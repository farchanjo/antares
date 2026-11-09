#include "adapters/http/client.hpp"

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
    std::shared_ptr<Connection> connection_;
    H1Session session_;
    std::uint8_t hops_ = 0;
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

HttpClient::HttpClient(ports::io::IoEngine &engine, domain::http::ClientConfig config)
    : engine_(engine)
    , config_(std::move(config))
{
    tls_ = TlsContext::create(config_.verifyPeer, /*http2=*/false);
}

HttpClient::~HttpClient() = default;

std::expected<std::shared_ptr<ports::http::Ticket>, domain::http::HttpError>
HttpClient::submit(domain::http::Request request, ports::http::Completion done)
{
    auto parsed = domain::http::Url::parse(request.url);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (!config_.verifyPeer && !parsed->isTls()) {
        // Plaintext is allowed, but only when the caller opted out of TLS.
    }

    auto attempt = std::make_shared<Attempt>(std::move(request), std::move(done), config_);
    attempt->url_ = std::move(*parsed);

    // Hop to the engine thread; everything below assumes it (FR-002).
    engine_.post([this, attempt] { start(attempt); });
    return std::static_pointer_cast<ports::http::Ticket>(attempt);
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
    attempt->connection_ = acquire(attempt->url_);
    if (attempt->connection_) {
        ++stats_.reused;
        onReady(attempt);
        return;
    }

    auto connection = std::make_shared<Connection>(engine_, *tls_);
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

void HttpClient::onReady(const std::shared_ptr<Attempt> &attempt)
{
    std::string wire = H1Session::serialize(attempt->request_, attempt->url_.target,
                                            attempt->url_.authority, config_.userAgent);

    auto self = std::static_pointer_cast<HttpClient>(shared_from_this());
    auto bytes = ports::io::Buffer{
        reinterpret_cast<const std::byte *>(wire.data()),
        reinterpret_cast<const std::byte *>(wire.data()) + wire.size()};

    attempt->connection_->write(
        std::move(bytes), [self, attempt](std::expected<std::size_t, domain::http::HttpError> wrote) {
            if (attempt->cancelled()) {
                return;
            }
            if (!wrote) {
                self->finish(attempt, std::unexpected(wrote.error()));
                return;
            }
            self->readMore(attempt);
        });
}

void HttpClient::readMore(const std::shared_ptr<Attempt> &attempt)
{
    auto self = std::static_pointer_cast<HttpClient>(shared_from_this());
    attempt->connection_->read(
        READ_CHUNK,
        [self, attempt](std::expected<ports::io::Buffer, domain::http::HttpError> chunk) {
            if (attempt->cancelled()) {
                return;
            }
            if (!chunk) {
                if (attempt->session_.isComplete()) {
                    self->followRedirect(attempt, attempt->session_.take());
                    return;
                }
                self->finish(attempt, std::unexpected(chunk.error()));
                return;
            }

            auto consumed = attempt->session_.consume(chunk->data(), chunk->size());
            if (!consumed) {
                self->finish(attempt, std::unexpected(consumed.error()));
                return;
            }
            if (attempt->session_.isComplete()) {
                self->followRedirect(attempt, attempt->session_.take());
                return;
            }
            self->readMore(attempt);
        });
}

void HttpClient::followRedirect(const std::shared_ptr<Attempt> &attempt,
                                domain::http::Response response)
{
    const bool followable = config_.redirects.follow && response.isRedirect();
    const std::string *location = response.find("Location");

    if (!followable || location == nullptr) {
        finish(attempt, std::move(response));
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

void HttpClient::finish(const std::shared_ptr<Attempt> &attempt,
                        std::expected<domain::http::Response, domain::http::HttpError> result)
{
    if (attempt->finished()) {
        return;
    }

    if (result) {
        result->hops = attempt->hops_;
        if (attempt->connection_ && attempt->connection_->isReady()) {
            release(std::move(attempt->connection_));
        }
    } else if (attempt->connection_) {
        attempt->connection_->close();
    }

    ++stats_.completed;
    auto done = std::move(attempt->done_);
    attempt->done_ = nullptr;
    done(std::move(result));
}

std::shared_ptr<Connection> HttpClient::acquire(const domain::http::Url &url)
{
    auto found = idle_.find(url.authority);
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

    auto &bucket = idle_[connection->url().authority];
    if (bucket.size() >= config_.pool.maxIdlePerHost) {
        connection->close();
        ++stats_.idleClosed;
        return;
    }
    bucket.push_back(std::move(connection));
}

} // namespace antares::adapters::http
