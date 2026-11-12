#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "adapters/http/connection.hpp"
#include "domain/http/cache.hpp"
#include "domain/http/cookie_jar.hpp"
#include "adapters/http/h1_session.hpp"
#include "adapters/http/h2_session.hpp"
#include "adapters/http/websocket.hpp"
#include "adapters/http/tls_context.hpp"
#include "application/io/engine_pool.hpp"
#include "ports/http/client.hpp"

namespace antares::adapters::http {

/// HTTP client over the engine. One attempt is bound to one loop for its whole
/// life, but the pool it comes back to is shared by every loop, so the pool
/// state (`idle_`, `sessions_`, `inflight_`, `current_`) is guarded by one
/// mutex (FR-002). No completion ever waits on that mutex. Clients fan work
/// across loops; a single-loop pool never contends.
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

    [[nodiscard]] std::size_t pending() const noexcept override
    {
        return pending_.load(std::memory_order_acquire);
    }

    /// Open a WebSocket: sends the upgrade and hands over the socket on 101.
    /// h1 only — the h2 path would need RFC 8441 extended CONNECT.
    using SocketReady = std::function<void(std::shared_ptr<WebSocket>)>;
    using SocketFailed = std::function<void(domain::http::HttpError)>;

    void openWebSocket(domain::http::Url url, std::string subprotocol, SocketReady ready,
                       SocketFailed failed);

    /// The port's spelling of the same upgrade. One implementation, two names:
    /// the concrete class keeps the one its own callers already use.
    void openSocket(domain::http::Url url, std::string subprotocol,
                    ports::http::Client::SocketReady ready,
                    ports::http::Client::SocketFailed failed) override;

private:
    class Attempt;

    void start(const std::shared_ptr<Attempt> &attempt);
    void armIdleTimer(const std::shared_ptr<Attempt> &attempt);
    void resetSession(const std::shared_ptr<Attempt>& attempt) const;
    void log(ports::log::Level level, std::string_view message) const;

    /// Undo the `content-encoding` the peer applied. An answer this build cannot
    /// decode fails as a protocol error rather than reaching the caller as bytes.
    void decodeContent(domain::http::Response &response) const;

    /// Record every `set-cookie` the response carries, against the URL that was
    /// actually requested.
    void storeCookies(const domain::http::Response &response);

    /// The `authorization` line for a hand-written request, or nothing.
    [[nodiscard]] std::string authorizationHead() const;

    /// Revalidate: keep a validator from a 200, and hand back the stored answer
    /// when the origin answers 304.
    void cached(domain::http::Response &response);

    [[nodiscard]] static std::string cacheKey(const domain::http::Url &url);
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
    /// h1-only, for the upgrade path. Never used for ordinary requests.
    std::unique_ptr<TlsContext> tlsUpgrade_;
    domain::http::CookieJar jar_;
    domain::http::ConditionalCache cache_;
    /// The URL of the request in flight, for the cookie scope. Guarded by
    /// `stateMutex_`.
    domain::http::Url current_;
    /// Guards the state every loop shares: an attempt on one loop may release
    /// while an attempt on another is acquiring, and neither owns the map.
    /// Recursive because dropping a connection (or replacing an h2 session)
    /// destroys a callback that may hold the last reference to this client, and
    /// that destructor takes the same lock.
    mutable std::recursive_mutex stateMutex_;
    /// Keyed by loop *and* authority: a connection may only be reused from
    /// the loop that owns it. Guarded by `stateMutex_`.
    std::map<std::pair<ports::io::IoEngine *, std::string>,
             std::vector<std::shared_ptr<Connection>>>
        idle_;

    /// One multiplexed session per loop and origin (FR-019). Guarded by
    /// `stateMutex_`.
    std::map<std::pair<ports::io::IoEngine *, std::string>, std::shared_ptr<H2Session>> sessions_;
    /// Attempts in flight, and the latch the destructor waits on. A completion
    /// captures `this`, so the client may not die while one is still pending.
    /// Attempts in flight. Without an owner the only references are the
    /// callbacks the connections hold, and a connection that also pointed back at
    /// the client closed the cycle (FR-026). Guarded by `stateMutex_`.
    std::vector<std::shared_ptr<Attempt>> inflight_;

    /// Drop the attempt and its pending share. Safe to call twice.
    void retire(const std::shared_ptr<Attempt> &attempt) noexcept;

    std::atomic<std::size_t> pending_{0};
    std::mutex drainMutex_;
    std::condition_variable drainCv_;

    ports::http::ClientStats stats_{};
    std::uint64_t rng_ = 0x9e3779b97f4a7c15ULL;
};

} // namespace antares::adapters::http
