#pragma once

#include <map>
#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "adapters/http/connection.hpp"
#include "adapters/http/h1_session.hpp"
#include "adapters/http/h2_session.hpp"
#include "adapters/http/tls_context.hpp"
#include "application/io/engine_pool.hpp"
#include "ports/http/client.hpp"

namespace antares::adapters::http {

/// HTTP client over the engine. All state lives on the engine thread, so no
/// mutex guards the pool and a completion never waits (FR-002).
class HttpClient final : public ports::http::Client,
                         public std::enable_shared_from_this<HttpClient> {
public:
    HttpClient(application::io::EnginePool &pool, domain::http::ClientConfig config);
    ~HttpClient() override;

    HttpClient(const HttpClient &) = delete;
    HttpClient &operator=(const HttpClient &) = delete;

    [[nodiscard]] const domain::http::ClientConfig &config() const noexcept override
    {
        return config_;
    }

    [[nodiscard]] std::expected<std::shared_ptr<ports::http::Ticket>, domain::http::HttpError>
    submit(domain::http::Request request, ports::http::Completion done) override;

    [[nodiscard]] std::expected<std::shared_ptr<ports::http::Ticket>, domain::http::HttpError>
    submitStreaming(domain::http::Request request, ports::http::StreamHandler handler,
                    ports::http::Completion done) override;

    [[nodiscard]] ports::http::ClientStats stats() const noexcept override { return stats_; }

private:
    class Attempt;

    void start(const std::shared_ptr<Attempt> &attempt);
    void armIdleTimer(const std::shared_ptr<Attempt> &attempt);
    void connect(const std::shared_ptr<Attempt> &attempt);
    void onReady(const std::shared_ptr<Attempt> &attempt);
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

    void startHttp2(const std::shared_ptr<Attempt> &attempt);
    void pump(const std::shared_ptr<H2Session> &session,
              const std::shared_ptr<Attempt> &attempt);
    [[nodiscard]] std::shared_ptr<H2Session> sessionFor(ports::io::IoEngine &engine,
                                                       const std::string &authority,
                                                       std::shared_ptr<Connection> connection);
    void release(std::shared_ptr<Connection> connection);
    [[nodiscard]] std::shared_ptr<Connection> acquire(ports::io::IoEngine &engine,
                                                      const domain::http::Url &url);

    application::io::EnginePool &pool_;
    domain::http::ClientConfig config_;
    std::unique_ptr<TlsContext> tls_;
    /// Keyed by loop *and* authority: a connection may only be reused from
    /// the loop that owns it.
    std::map<std::pair<ports::io::IoEngine *, std::string>,
             std::vector<std::shared_ptr<Connection>>>
        idle_;

    /// One multiplexed session per loop and origin (FR-019).
    std::map<std::pair<ports::io::IoEngine *, std::string>, std::shared_ptr<H2Session>> sessions_;
    ports::http::ClientStats stats_{};
    std::uint64_t rng_ = 0x9e3779b97f4a7c15ULL;
};

} // namespace antares::adapters::http
