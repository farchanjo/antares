#include "adapters/http/client.hpp"

#include "adapters/http/content_decoder.hpp"

namespace {
constexpr std::string_view AUTHORIZATION = "authorization";
}

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <expected>
#include <utility>

namespace antares::adapters::http {
namespace {

constexpr domain::Bytes READ_CHUNK{std::size_t{16} * 1024};

} // namespace

/// One in-flight request. Also the ticket handed to the caller.
class HttpClient::Attempt : public ports::http::Ticket, public std::enable_shared_from_this<Attempt> {
public:
    /// Completes once with `cancelled` and releases the wire: an h1 connection
    /// is closed, an h2 stream is reset and the connection survives.
    void cancel() override
    {
        if (!done_) {
            return;
        }
        cancelled_ = true;
        auto done = std::move(done_);
        done_ = nullptr;
        done(std::unexpected(domain::http::HttpError::cancelled));
        releaseWire();
    }

    void releaseWire()
    {
        if (h2_ && streamId_ > 0) {
            h2_->reset(streamId_);
            streamId_ = 0;
            return;
        }
        if (connection_) {
            // Keep-alive survives: only the pending op is withdrawn.
            connection_->cancelPending();
        }
    }

    [[nodiscard]] bool cancelled() const noexcept { return cancelled_; }
    [[nodiscard]] bool finished() const noexcept { return !done_; }

    domain::http::Request request_;
    domain::http::Url url_;
    ports::io::IoEngine *engine_ = nullptr;
    std::shared_ptr<Connection> connection_;
    H1Session session_;
    std::uint8_t hops_ = 0;
    domain::http::Method method_ = domain::http::Method::get;
    ports::http::StreamHandler stream;
    std::uint64_t timerGeneration = 0;
    std::shared_ptr<H2Session> h2_;
    std::int32_t streamId_ = 0;
    std::uint8_t attempts_ = 1;
    bool cancelled_ = false;
    ports::http::Completion done_;

    Attempt(domain::http::Request request, ports::http::Completion done,
            const domain::http::ClientConfig &config)
        : request_(std::move(request))
        , done_(std::move(done))
        , session_(config.limits.maxHeaderBytes, config.limits.maxBodyBytes)
    {
    }
};

HttpClient::HttpClient(application::io::EnginePool &pool, domain::http::ClientConfig config)
    : pool_(pool)
    , config_(std::move(config))
{
    tls_ = TlsContext::create(config_.verifyPeer, /*http2=*/true, config_.auth,
                              config_.trustBundlePem);
    // A WebSocket upgrade is an HTTP/1.1 exchange with no h2 equivalent, so it
    // needs a context that never offers h2 in ALPN. Sharing one context meant the
    // server picked h2 and answered the hand-written upgrade with SETTINGS frames.
    tlsUpgrade_ = TlsContext::create(config_.verifyPeer, /*http2=*/false, config_.auth,
                                     config_.trustBundlePem);
    pool_.setLogger(config_.logger);
}

HttpClient::~HttpClient()
{
    // A completion runs on the loop and captures `this`, so every pending
    // attempt has to settle before the members go. On a loop thread the wait
    // would deadlock, so it is skipped there and the pool owns the lifetime.
    if (pool_.isLoopThread()) {
        return;
    }
    if (!pool_.isRunning()) {
        // No loop to post to, so the members are dropped in place. `close` is
        // not called here on purpose: it re-enters the engine, and during
        // teardown the engine may be the thing being destroyed.
        idle_.clear();
        sessions_.clear();
        return;
    }
    // Hand every socket back to the loop that owns it. The post captures the
    // connection, so it outlives the member that named it and `this` is never
    // touched from the loop.
    for (auto &[key, bucket] : idle_) {
        for (auto &connection : bucket) {
            if (!connection) {
                continue;
            }
            auto owned = connection;
            key.first->post([owned] { owned->close(); });
        }
    }
    for (auto &[key, session] : sessions_) {
        if (!session) {
            continue;
        }
        auto socket = session->connection();
        if (!socket) {
            continue;
        }
        key.first->post([socket] { socket->close(); });
    }

    std::unique_lock<std::mutex> lock{drainMutex_};
    (void)drainCv_.wait_for(lock, std::chrono::seconds{5},
                            [this] { return pending_.load(std::memory_order_acquire) == 0; });
}

std::expected<std::shared_ptr<ports::http::Ticket>, domain::http::HttpError>
HttpClient::submit(domain::http::Request request, ports::http::Completion done)
{
    return submitStreaming(std::move(request), ports::http::StreamHandler{}, std::move(done));
}

std::expected<std::shared_ptr<ports::http::Ticket>, domain::http::HttpError>
HttpClient::submitStreaming(domain::http::Request request, ports::http::StreamHandler handler,
                            ports::http::Completion done)
{
    auto parsed = domain::http::Url::parse(request.url);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (!config_.verifyPeer && !parsed->isTls()) {
        // Plaintext is allowed, but only when the caller opted out of TLS.
    }

    const domain::http::Method method = request.method;
    auto credential = config_.auth.authorizationValue();
    if (!credential) {
        return std::unexpected(credential.error());
    }
    if (!credential->empty()) {
        const bool explicitHeader =
            std::any_of(request.headers.begin(), request.headers.end(),
                        [](const domain::http::Header &header) { return header.name == "Authorization"; });
        if (!explicitHeader) {
            request.headers.push_back(domain::http::Header{"Authorization", *credential});
        }
    }

    // Offered once, here, so no caller has to remember it. A caller that named
    // its own wins.
    const bool acceptsCoded =
        std::any_of(request.headers.begin(), request.headers.end(),
                    [](const domain::http::Header &header) { return header.name == "Accept-Encoding"; });
    if (!acceptsCoded) {
        request.headers.push_back(
            domain::http::Header{"Accept-Encoding", std::string{acceptedEncodings()}});
    }
    const bool decode = config_.acceptEncoding;

    // The counter is decremented only after the caller's completion returns, so
    // the destructor never resumes while a callback still uses this client.
    auto user = std::move(done);
    auto guarded = [this, user = std::move(user), decode](
                       std::expected<domain::http::Response, domain::http::HttpError> result) {
        if (result && decode) {
            decodeContent(*result);
        }
        user(std::move(result));
        pending_.fetch_sub(1, std::memory_order_acq_rel);
        drainCv_.notify_all();
    };
    pending_.fetch_add(1, std::memory_order_acq_rel);

    auto attempt = std::make_shared<Attempt>(std::move(request), std::move(guarded), config_);
    attempt->stream = std::move(handler);
    attempt->method_ = method;
    resetSession(attempt);
    attempt->url_ = std::move(*parsed);

    // Bind the attempt to one loop for its whole life; hop there now, so every
    // step below runs on that loop (FR-002).
    attempt->engine_ = &pool_.next();
    inflight_.push_back(attempt);
    armIdleTimer(attempt);
    attempt->engine_->post([this, attempt] {
        // A live session already owns the connection for this origin; opening
        // another one would just be closed again.
        auto found = sessions_.find({attempt->engine_, attempt->url_.authority});
        if (found != sessions_.end() && found->second && !found->second->isClosed()) {
            startHttp2(attempt);
            return;
        }
        start(attempt);
    });
    return std::static_pointer_cast<ports::http::Ticket>(attempt);
}

void HttpClient::log(ports::log::Level level, std::string_view message) const
{
    if (config_.logger != nullptr) {
        config_.logger->write(level, "http", message);
    }
}

void HttpClient::resetSession(const std::shared_ptr<Attempt>& attempt) const {
    // A fresh session starts empty, so the stream sinks and the method are
    // reattached here rather than at every call site.
    attempt->session_ = H1Session{config_.limits.maxHeaderBytes, config_.limits.maxBodyBytes,
                                  attempt->method_};
    if (attempt->stream.onChunk) {
        attempt->session_.onChunk(attempt->stream.onChunk);
    }
    if (attempt->stream.onHead) {
        attempt->session_.onHead(attempt->stream.onHead);
    }
}

void HttpClient::armIdleTimer(const std::shared_ptr<Attempt> &attempt)
{
    if (config_.timeouts.read.count() <= 0) {
        return;
    }

    const std::uint64_t generation = ++attempt->timerGeneration;
    auto self = std::static_pointer_cast<HttpClient>(shared_from_this());
    static_cast<void>(attempt->engine_->submitTimer(
        config_.timeouts.read, [self, attempt, generation] {
            if (attempt->finished() || generation != attempt->timerGeneration) {
                return;
            }
            ++self->stats_.timeouts;
            self->log(ports::log::Level::warn, "idle timeout");
            self->settle(attempt, std::unexpected(domain::http::HttpError::timeout));
        }));
}

void HttpClient::start(const std::shared_ptr<Attempt> &attempt)
{
    if (attempt->cancelled()) {
        return;
    }
    connect(attempt);
}

void HttpClient::connect(const std::shared_ptr<Attempt> &attempt)
{
    attempt->connection_ = acquire(*attempt->engine_, attempt->url_);
    if (attempt->connection_) {
        ++stats_.reused;
        onReady(attempt);
        return;
    }

    auto connection = std::make_shared<Connection>(*attempt->engine_, *tls_, config_.logger);
    attempt->connection_ = connection;
    ++stats_.opened;

    // Weak throughout: `open` stores these on the connection, which the client
    // owns, so a strong capture would close the cycle on every attempt.
    auto weak = weak_from_this();
    // Both captures are weak: the connection is reachable from the attempt and
    // the attempt is owned by `inflight_`, so a strong capture here would close
    // the cycle and leak the pair.
    const std::weak_ptr<Attempt> watched = attempt;
    connection->open(
        attempt->url_,
        [weak, watched] {
            const auto attempt = watched.lock();
            const auto self = weak.lock();
            if (!attempt || !self || attempt->cancelled()) {
                return;
            }
            self->onReady(attempt);
        },
        [weak, watched](domain::http::HttpError error) {
            const auto attempt = watched.lock();
            const auto self = weak.lock();
            if (!attempt || !self) {
                return;
            }
            if (error == domain::http::HttpError::tls) {
                ++self->stats_.tlsFailures;
                self->log(ports::log::Level::error, "tls handshake failed");
            }
            self->finish(attempt, std::unexpected(error));
        });
}

void HttpClient::openWebSocket(domain::http::Url url, std::string subprotocol, SocketReady ready,
                               SocketFailed failed)
{
    auto self = std::static_pointer_cast<HttpClient>(shared_from_this());
    auto connection = std::make_shared<Connection>(pool_.next(), *tlsUpgrade_, config_.logger);
    auto* engine = &connection->engine();

    engine->post([self, connection, url = std::move(url), subprotocol = std::move(subprotocol),
                  ready = std::move(ready), failed = std::move(failed)]() mutable {
        connection->open(
            url,
            [self, connection, url, subprotocol = std::move(subprotocol),
             ready = std::move(ready)]() mutable {
                auto socket = std::make_shared<WebSocket>(connection, self->config_.limits.maxBodyBytes);

                // The 101 head shares the read with the first frame, so it is
                // consumed here and only the remainder reaches the codec.
                // RFC 6455 §4.1: the key is sixteen random bytes in base64, and
                // the server proves it read the request by echoing a digest of
                // it. A key we could predict would make that proof worthless.
                const std::string key = WebSocket::newKey();

                auto pending = std::make_shared<std::string>();
                auto upgraded = std::make_shared<bool>(false);
                connection->onData([socket, pending, upgraded, key,
                                    offered = subprotocol](const std::byte *data,
                                                           std::size_t length) {
                    if (*upgraded) {
                        socket->receive(data, length);
                        return;
                    }
                    pending->append(reinterpret_cast<const char *>(data), length);
                    const std::size_t end = pending->find("\r\n\r\n");
                    if (end == std::string::npos) {
                        return;
                    }
                    if (pending->rfind("HTTP/1.1 101", 0) != 0) {
                        socket->abort(domain::http::HttpError::protocol);
                        return;
                    }
                    // The terminator is part of what the check reads: without
                    // it the last header has no boundary.
                    if (!WebSocket::verifyHandshake(
                            std::string_view{pending->data(), end + 4}, key, offered)) {
                        // A wrong accept means the peer never read our key, and
                        // a missing subprotocol means it will frame in a way we
                        // did not agree to. Either way, do not proceed.
                        socket->abort(domain::http::HttpError::protocol);
                        return;
                    }

                    *upgraded = true;
                    const auto *rest = reinterpret_cast<const std::byte *>(pending->data()) + end + 4;
                    const std::size_t remaining = pending->size() - end - 4;
                    if (remaining > 0) {
                        socket->receive(rest, remaining);
                    }
                });

                // RFC 6455 §4.1: an upgrade request is a normal GET with the
                // handshake headers, and the response must be 101.
                std::string request = "GET " + (url.target.empty() ? "/" : url.target) +
                                      " HTTP/1.1\r\nHost: " + url.authority +
                                      "\r\nUpgrade: websocket\r\nConnection: Upgrade"
                                      "\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " +
                                      key + "\r\n";
                if (!subprotocol.empty()) {
                    request += "Sec-WebSocket-Protocol: " + subprotocol + "\r\n";
                }
                request += "\r\n";

                connection->write(
                    ports::io::Buffer{reinterpret_cast<const std::byte *>(request.data()),
                                      reinterpret_cast<const std::byte *>(request.data()) +
                                          request.size()},
                    [socket](WriteOutcome wrote) {
                        if (!wrote.ok) {
                            socket->abort(domain::http::HttpError::closed);
                        }
                    });
                ready(std::move(socket));
            },
            std::move(failed));
    });
}

std::shared_ptr<H2Session> HttpClient::sessionFor(ports::io::IoEngine &engine,
                                                  const std::string &authority,
                                                  std::shared_ptr<Connection> connection)
{
    auto &slot = sessions_[{&engine, authority}];
    if (slot && !slot->isClosed()) {
        return slot;
    }

    if (!connection) {
        connection = std::make_shared<Connection>(engine, *tls_, config_.logger);
    }
    slot = std::make_shared<H2Session>(connection, config_.limits.maxHeaderBytes,
                                       config_.limits.maxBodyBytes);
    return slot;
}

void HttpClient::pump(const std::shared_ptr<H2Session> &session,
                      const std::shared_ptr<Attempt> &attempt)
{
    // Weak: the connection is owned by the client, so a strong capture here would
    // close the cycle and neither would ever be freed.
    auto weak = weak_from_this();
    session->connection()->onData([weak, session, attempt](const std::byte *data,
                                                           std::size_t length) {
        if (const auto self = weak.lock()) {
            self->armIdleTimer(attempt);
        }
        session->receive(data, length);
    });
}

void HttpClient::startHttp2(const std::shared_ptr<Attempt> &attempt)
{
    auto self = std::static_pointer_cast<HttpClient>(shared_from_this());
    auto session = sessionFor(*attempt->engine_, attempt->url_.authority, attempt->connection_);
    attempt->h2_ = session;

    auto submitNow = [self, session, attempt] {
        auto submitted = session->submitStreaming(
            attempt->request_, attempt->url_.target, attempt->url_.authority, attempt->stream,
            [self, attempt](auto result) { self->settle(attempt, std::move(result)); });
        if (!submitted) {
            self->settle(attempt, std::unexpected(submitted.error()));
            return;
        }
        attempt->streamId_ = *submitted;
    };

    const auto &connection = session->connection();
    if (connection->isReady()) {
        pump(session, attempt);
        submitNow();
        return;
    }

    connection->open(
        attempt->url_, [self, session, submitNow, attempt] {
            self->pump(session, attempt);
            submitNow();
        },
        [self, session](domain::http::HttpError error) { session->fail(error); });
}

void HttpClient::onReady(const std::shared_ptr<Attempt> &attempt)
{
    if (attempt->connection_ && attempt->connection_->isHttp2()) {
        startHttp2(attempt);
        return;
    }

    auto weak = weak_from_this();
    attempt->connection_->onData([weak, attempt](const std::byte *data, std::size_t length) {
        const auto self = weak.lock();
        if (!self || attempt->cancelled()) {
            return;
        }
        self->armIdleTimer(attempt);
        auto consumed = attempt->session_.consume(data, length);
        if (!consumed) {
            self->settle(attempt, std::unexpected(consumed.error()));
            return;
        }
        if (attempt->session_.isComplete()) {
            self->followRedirect(attempt, attempt->session_.take());
        }
    });

    const auto self = weak.lock();
    if (!self) {
        return;
    }

    std::string wire = H1Session::serialize(attempt->request_, attempt->url_.target,
                                            attempt->url_.authority, config_.userAgent);

    auto bytes = ports::io::Buffer{
        reinterpret_cast<const std::byte *>(wire.data()),
        reinterpret_cast<const std::byte *>(wire.data()) + wire.size()};

    // Weak for the same reason as the read handler: the connection outlives the
    // call and is owned by the client.
    const auto weakSelf = weak_from_this();
    attempt->connection_->write(
        std::move(bytes), [weakSelf, attempt](WriteOutcome wrote) {
            if (attempt->cancelled()) {
                return;
            }
            if (const auto owner = weakSelf.lock(); !wrote.ok && owner) {
                owner->settle(attempt, std::unexpected(wrote.error));
            }
        });
}

void HttpClient::followRedirect(const std::shared_ptr<Attempt> &attempt,
                                domain::http::Response response)
{
    if (response.isRedirect()) {
        log(ports::log::Level::trace, "redirect received");
    }
    const bool followable = config_.redirects.follow && response.isRedirect();
    const std::string *location = response.find("Location");

    if (!followable || location == nullptr) {
        settle(attempt, std::move(response));
        return;
    }

    if (attempt->hops_ >= config_.limits.maxRedirects) {
        finish(attempt, std::unexpected(domain::http::HttpError::tooManyRedirects));
        return;
    }

    auto target = domain::http::Url::resolve(attempt->url_, *location);
    if (!target) {
        finish(attempt, std::unexpected(target.error()));
        return;
    }

    const bool crossOrigin = !attempt->url_.sameOrigin(*target);
    release(std::move(attempt->connection_));

    // 303 always becomes GET; 301/302 keep the method only for GET/HEAD.
    if (response.status == 303 || (response.status != 307 && response.status != 308 &&
                                   attempt->request_.method != domain::http::Method::get &&
                                   attempt->request_.method != domain::http::Method::head)) {
        attempt->request_.method = domain::http::Method::get;
        attempt->request_.body.clear();
    }

    if (crossOrigin && !config_.redirects.forwardAuth) {
        auto &headers = attempt->request_.headers;
        headers.erase(std::remove_if(headers.begin(), headers.end(),
                                     [](const domain::http::Header &header) {
                                         // Field names are case-insensitive (§5.1):
                                         // an exact compare would let a lowercase
                                         // `authorization` cross to another origin.
                                         return std::equal(
                                             header.name.begin(), header.name.end(),
                                             AUTHORIZATION.begin(), AUTHORIZATION.end(),
                                             [](char left, char right) {
                                                 return std::tolower(static_cast<unsigned char>(
                                                            left)) == right;
                                             });
                                     }),
                      headers.end());
    }

    attempt->url_ = std::move(*target);
    resetSession(attempt);
    ++attempt->hops_;
    ++stats_.redirects;

    connect(attempt);
}

bool HttpClient::shouldRetry(const std::shared_ptr<Attempt> &attempt,
                             const std::expected<domain::http::Response,
                                                 domain::http::HttpError> &result) const
{
    const domain::http::RetryPolicy &policy = config_.retry;
    if (attempt->cancelled() || attempt->attempts_ >= policy.maxAttempts) {
        return false;
    }

    const bool idempotent = attempt->request_.method == domain::http::Method::get ||
                            attempt->request_.method == domain::http::Method::head ||
                            attempt->request_.method == domain::http::Method::put ||
                            attempt->request_.method == domain::http::Method::del ||
                            attempt->request_.method == domain::http::Method::options;
    if (!idempotent && !policy.retryNonIdempotent) {
        return false;
    }

    if (result) {
        return policy.isRetryableStatus(result->status);
    }

    switch (result.error()) {
    case domain::http::HttpError::connect:
    case domain::http::HttpError::closed:
        return policy.retryOnConnect;
    case domain::http::HttpError::timeout:
        return policy.retryOnTimeout;
    default:
        return false;
    }
}

domain::Duration HttpClient::backoffFor(std::uint8_t attempt)
{
    const domain::http::RetryPolicy &policy = config_.retry;

    auto delay = static_cast<double>(policy.baseDelay.count());
    for (std::uint8_t step = 1; step < attempt; ++step) {
        delay *= policy.factor;
    }
    delay = std::min(delay, static_cast<double>(policy.maxDelay.count()));

    if (policy.jitter > 0.0) {
        // xorshift64: no <random> state, and the loop thread is the only caller.
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 7;
        rng_ ^= rng_ << 17;
        const double unit = static_cast<double>(rng_ % 1000) / 1000.0;
        delay *= 1.0 - policy.jitter + (2.0 * policy.jitter * unit);
    }

    return domain::Duration{static_cast<std::int64_t>(delay)};
}

void HttpClient::scheduleRetry(const std::shared_ptr<Attempt> &attempt, domain::Duration delay)
{
    ++attempt->attempts_;
    ++stats_.retries;
    log(ports::log::Level::debug, "retrying the request");
    auto self = std::static_pointer_cast<HttpClient>(shared_from_this());
    static_cast<void>(
        attempt->engine_->submitTimer(delay, [self, attempt] { self->start(attempt); }));
}

void HttpClient::retire(const std::shared_ptr<Attempt> &attempt) noexcept
{
    const auto slot = std::find(inflight_.begin(), inflight_.end(), attempt);
    if (slot != inflight_.end()) {
        inflight_.erase(slot);
    }
}

void HttpClient::settle(const std::shared_ptr<Attempt> &attempt,
                        std::expected<domain::http::Response, domain::http::HttpError> result)
{
    if (shouldRetry(attempt, result)) {
        domain::Duration delay = backoffFor(attempt->attempts_);

        if (result && config_.retry.honorRetryAfter) {
            if (const std::string *after = result->find("Retry-After"); after != nullptr) {
                std::uint32_t seconds = 0;
                if (std::from_chars(after->data(), after->data() + after->size(), seconds).ec ==
                        std::errc{} &&
                    seconds > 0) {
                    delay = std::chrono::seconds{seconds};
                }
            }
        }

        // The session owns an h2 connection; only h1 goes back to the pool.
        if (attempt->connection_ && !attempt->connection_->isHttp2()) {
            if (result && attempt->connection_->isReady()) {
                release(std::move(attempt->connection_));
            } else {
                attempt->connection_->close();
            }
        }

        resetSession(attempt);
        scheduleRetry(attempt, delay);
        return;
    }

    finish(attempt, std::move(result));
}

void HttpClient::decodeContent(domain::http::Response &response) const
{
    const std::string *coded = response.find("content-encoding");
    if (coded == nullptr) {
        return;
    }
    const std::vector<ContentEncoding> encodings = parseEncodings(*coded);
    if (encodings.empty()) {
        return;
    }
    if (std::find(encodings.begin(), encodings.end(), ContentEncoding::unknown) !=
        encodings.end()) {
        log(ports::log::Level::warn, "undecodable content-encoding: " + *coded);
        return;
    }

    const std::string_view codedBytes{reinterpret_cast<const char *>(response.body.data()),
                                      response.body.size()};
    auto decoded = decodeBody(encodings, codedBytes);
    if (!decoded) {
        log(ports::log::Level::warn, "content-encoding could not be undone: " + *coded);
        return;
    }
    response.body.assign(reinterpret_cast<const std::byte *>(decoded->data()),
                         reinterpret_cast<const std::byte *>(decoded->data()) + decoded->size());
}

void HttpClient::finish(const std::shared_ptr<Attempt> &attempt,
                        std::expected<domain::http::Response, domain::http::HttpError> result)
{
    if (attempt->finished()) {
        return;
    }
    retire(attempt);

    if (result) {
        result->hops = attempt->hops_;
    }
    if (attempt->connection_ && !attempt->connection_->isHttp2()) {
        if (result && attempt->connection_->isReady() && attempt->session_.isReusable()) {
            release(std::move(attempt->connection_));
        } else if (!result || !attempt->session_.isReusable()) {
            attempt->connection_->close();
        }
    }

    ++stats_.completed;
    auto done = std::move(attempt->done_);
    attempt->done_ = nullptr;
    done(std::move(result));
}

std::shared_ptr<Connection> HttpClient::acquire(ports::io::IoEngine &engine,
                                                const domain::http::Url &url)
{
    auto found = idle_.find({&engine, url.authority});
    if (found == idle_.end() || found->second.empty()) {
        return nullptr;
    }
    auto connection = found->second.back();
    found->second.pop_back();
    return connection;
}

void HttpClient::release(std::shared_ptr<Connection> connection)
{
    if (!connection || !connection->isReady()) {
        return;
    }

    auto &bucket = idle_[{&connection->engine(), connection->url().authority}];
    if (bucket.size() >= config_.pool.maxIdlePerHost) {
        connection->close();
        ++stats_.idleClosed;
        return;
    }
    bucket.push_back(std::move(connection));
}

} // namespace antares::adapters::http

namespace antares::adapters::http {

void HttpClient::openSocket(domain::http::Url url, std::string subprotocol,
                            ports::http::Client::SocketReady ready,
                            ports::http::Client::SocketFailed failed)
{
    // The concrete class already hands over a `WebSocket`; the port only sees
    // the interface it declares, so the pointer is upcast here and nowhere else.
    openWebSocket(std::move(url), std::move(subprotocol),
                  [ready = std::move(ready)](std::shared_ptr<WebSocket> socket) {
                      ready(std::move(socket));
                  },
                  std::move(failed));
}

} // namespace antares::adapters::http
