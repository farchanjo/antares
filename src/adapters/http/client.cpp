#include "adapters/http/client.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <utility>

namespace antares::adapters::http {
namespace {

constexpr domain::Bytes READ_CHUNK{16 * 1024};

} // namespace

/// One in-flight request. Also the ticket handed to the caller.
class HttpClient::Attempt : public ports::http::Ticket, public std::enable_shared_from_this<Attempt> {
public:
    void cancel() override
    {
        if (!done_) {
            return;
        }
        cancelled_ = true;
        auto done = std::move(done_);
        done_ = nullptr;
        done(std::unexpected(domain::http::HttpError::cancelled));
    }

    [[nodiscard]] bool cancelled() const noexcept { return cancelled_; }
    [[nodiscard]] bool finished() const noexcept { return !done_; }

    domain::http::Request request_;
    domain::http::Url url_;
    ports::io::IoEngine *engine_ = nullptr;
    std::shared_ptr<Connection> connection_;
    H1Session session_;
    std::uint8_t hops_ = 0;
    ports::http::StreamHandler stream;
    std::uint64_t timerGeneration = 0;
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
    tls_ = TlsContext::create(config_.verifyPeer, /*http2=*/true, config_.auth);
}

HttpClient::~HttpClient() = default;

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

    auto attempt = std::make_shared<Attempt>(std::move(request), std::move(done), config_);
    attempt->stream = std::move(handler);
    attempt->session_ = H1Session{config_.limits.maxHeaderBytes, config_.limits.maxBodyBytes, method};
    if (attempt->stream.onChunk) {
        attempt->session_.onChunk(attempt->stream.onChunk);
    }
    if (attempt->stream.onHead) {
        attempt->session_.onHead(attempt->stream.onHead);
    }
    attempt->url_ = std::move(*parsed);

    // Bind the attempt to one loop for its whole life; hop there now, so every
    // step below runs on that loop (FR-002).
    attempt->engine_ = &pool_.next();
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

void HttpClient::armIdleTimer(const std::shared_ptr<Attempt> &attempt)
{
    if (config_.timeouts.read.count() <= 0) {
        return;
    }

    const std::uint64_t generation = ++attempt->timerGeneration;
    auto self = std::static_pointer_cast<HttpClient>(shared_from_this());
    (void)attempt->engine_->submitTimer(config_.timeouts.read, [self, attempt, generation] {
        if (attempt->finished() || generation != attempt->timerGeneration) {
            return;
        }
        self->settle(attempt, std::unexpected(domain::http::HttpError::timeout));
    });
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

    auto connection = std::make_shared<Connection>(*attempt->engine_, *tls_);
    attempt->connection_ = connection;
    ++stats_.opened;

    auto self = std::static_pointer_cast<HttpClient>(shared_from_this());
    connection->open(
        attempt->url_,
        [self, attempt] {
            if (attempt->cancelled()) {
                return;
            }
            self->onReady(attempt);
        },
        [self, attempt](domain::http::HttpError error) {
            if (error == domain::http::HttpError::tls) {
                ++self->stats_.tlsFailures;
            }
            self->finish(attempt, std::unexpected(error));
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
        connection = std::make_shared<Connection>(engine, *tls_);
    }
    slot = std::make_shared<H2Session>(connection, config_.limits.maxHeaderBytes,
                                       config_.limits.maxBodyBytes);
    return slot;
}

void HttpClient::pump(const std::shared_ptr<H2Session> &session,
                      const std::shared_ptr<Attempt> &attempt)
{
    auto self = std::static_pointer_cast<HttpClient>(shared_from_this());
    session->connection()->onData([self, session, attempt](const std::byte *data,
                                                           std::size_t length) {
        self->armIdleTimer(attempt);
        session->receive(data, length);
    });
}

void HttpClient::startHttp2(const std::shared_ptr<Attempt> &attempt)
{
    auto self = std::static_pointer_cast<HttpClient>(shared_from_this());
    auto session = sessionFor(*attempt->engine_, attempt->url_.authority, attempt->connection_);

    auto submitNow = [self, session, attempt] {
        auto submitted = session->submitStreaming(
            attempt->request_, attempt->url_.target, attempt->url_.authority, attempt->stream,
            [self, attempt](auto result) { self->settle(attempt, std::move(result)); });
        if (!submitted) {
            self->settle(attempt, std::unexpected(submitted.error()));
        }
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
    std::fprintf(stderr, "  onReady h2=%d\n",
                 static_cast<int>(attempt->connection_ && attempt->connection_->isHttp2()));
    if (attempt->connection_ && attempt->connection_->isHttp2()) {
        startHttp2(attempt);
        return;
    }

    auto self = std::static_pointer_cast<HttpClient>(shared_from_this());
    attempt->connection_->onData([self, attempt](const std::byte *data, std::size_t length) {
        if (attempt->cancelled()) {
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

    std::string wire = H1Session::serialize(attempt->request_, attempt->url_.target,
                                            attempt->url_.authority, config_.userAgent);

    auto bytes = ports::io::Buffer{
        reinterpret_cast<const std::byte *>(wire.data()),
        reinterpret_cast<const std::byte *>(wire.data()) + wire.size()};

    attempt->connection_->write(
        std::move(bytes), [self, attempt](WriteOutcome wrote) {
            if (attempt->cancelled()) {
                return;
            }
            if (!wrote.ok) {
                self->settle(attempt, std::unexpected(wrote.error));
            }
        });
}

void HttpClient::followRedirect(const std::shared_ptr<Attempt> &attempt,
                                domain::http::Response response)
{
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
                                         return header.name == "Authorization";
                                     }),
                      headers.end());
    }

    attempt->url_ = std::move(*target);
    attempt->session_ = H1Session{config_.limits.maxHeaderBytes, config_.limits.maxBodyBytes};
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

    double delay = static_cast<double>(policy.baseDelay.count());
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
    auto self = std::static_pointer_cast<HttpClient>(shared_from_this());
    (void)attempt->engine_->submitTimer(delay, [self, attempt] { self->start(attempt); });
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

        attempt->session_ = H1Session{config_.limits.maxHeaderBytes, config_.limits.maxBodyBytes};
        scheduleRetry(attempt, delay);
        return;
    }

    finish(attempt, std::move(result));
}

void HttpClient::finish(const std::shared_ptr<Attempt> &attempt,
                        std::expected<domain::http::Response, domain::http::HttpError> result)
{
    if (attempt->finished()) {
        return;
    }

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
