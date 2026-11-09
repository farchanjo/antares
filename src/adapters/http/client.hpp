#pragma once

#include <map>
#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "adapters/http/connection.hpp"
#include "adapters/http/h1_session.hpp"
#include "adapters/http/tls_context.hpp"
#include "ports/http/client.hpp"

namespace antares::adapters::http {

/// HTTP client over the engine. All state lives on the engine thread, so no
/// mutex guards the pool and a completion never waits (FR-002).
class HttpClient final : public ports::http::Client,
                         public std::enable_shared_from_this<HttpClient> {
public:
    HttpClient(ports::io::IoEngine &engine, domain::http::ClientConfig config);
    ~HttpClient() override;

    HttpClient(const HttpClient &) = delete;
    HttpClient &operator=(const HttpClient &) = delete;

    [[nodiscard]] const domain::http::ClientConfig &config() const noexcept override
    {
        return config_;
    }

    [[nodiscard]] std::expected<std::shared_ptr<ports::http::Ticket>, domain::http::HttpError>
    submit(domain::http::Request request, ports::http::Completion done) override;

    [[nodiscard]] ports::http::ClientStats stats() const noexcept override { return stats_; }

private:
    class Attempt;

    void start(const std::shared_ptr<Attempt> &attempt);
    void connect(const std::shared_ptr<Attempt> &attempt);
    void onReady(const std::shared_ptr<Attempt> &attempt);
    void readMore(const std::shared_ptr<Attempt> &attempt);
    void finish(const std::shared_ptr<Attempt> &attempt,
                std::expected<domain::http::Response, domain::http::HttpError> result);
    void followRedirect(const std::shared_ptr<Attempt> &attempt, domain::http::Response response);
    void settle(const std::shared_ptr<Attempt> &attempt,
                std::expected<domain::http::Response, domain::http::HttpError> result);
    void scheduleRetry(const std::shared_ptr<Attempt> &attempt, domain::Duration delay);
    [[nodiscard]] bool shouldRetry(const std::shared_ptr<Attempt> &attempt,
                                   const std::expected<domain::http::Response,
                                                       domain::http::HttpError> &result) const;
    [[nodiscard]] domain::Duration backoffFor(std::uint8_t attempt);

    void release(std::shared_ptr<Connection> connection);
    [[nodiscard]] std::shared_ptr<Connection> acquire(const domain::http::Url &url);

    ports::io::IoEngine &engine_;
    domain::http::ClientConfig config_;
    std::unique_ptr<TlsContext> tls_;
    std::map<std::string, std::vector<std::shared_ptr<Connection>>> idle_;
    ports::http::ClientStats stats_{};
    std::uint64_t rng_ = 0x9e3779b97f4a7c15ULL;
};

} // namespace antares::adapters::http
