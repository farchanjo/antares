#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <functional>
#include <memory>

#include "domain/http/client_config.hpp"
#include "domain/http/http_error.hpp"
#include "domain/http/request.hpp"
#include "domain/http/response.hpp"
#include "domain/http/url.hpp"
#include "domain/http/websocket.hpp"
#include "ports/io/io_engine.hpp"

namespace antares::ports::http {

/// Opaque in-flight request. Cancelling is idempotent and safe from any thread.
class Ticket {
public:
    Ticket() = default;
    Ticket(const Ticket &) = delete;
    Ticket &operator=(const Ticket &) = delete;
    Ticket(Ticket &&) = delete;
    Ticket &operator=(Ticket &&) = delete;
    virtual ~Ticket() = default;

    virtual void cancel() = 0;
};

/// Live pool counters. Read-only; the client owns the values.
struct ClientStats {
    std::uint64_t opened = 0;
    std::uint64_t reused = 0;
    std::uint64_t completed = 0;
    std::uint64_t redirects = 0;
    std::uint64_t tlsFailures = 0;
    std::uint64_t retries = 0;
    std::uint64_t timeouts = 0;
    std::uint64_t idleClosed = 0;
};

/// Completion runs on the engine thread, exactly once (FR-007).
/// Incremental delivery. The head arrives once, then every body chunk as it
/// decrypts; a streaming response never buffers the whole body (FR-021).
struct StreamHandler {
    std::function<void(const domain::http::Response &)> onHead;
    std::function<void(std::span<const std::byte>)> onChunk;
};

using Completion =
    std::function<void(std::expected<domain::http::Response, domain::http::HttpError>)>;

/// Async HTTP client. Every call submits to the engine and returns; nothing here
/// blocks the loop (FR-002). Configuration is taken once, at construction.
/// A connection the client upgraded away from HTTP framing (FR-023).
///
/// The codec is not exposed: a caller sends payloads and receives frames. That
/// keeps RFC 6455 masking and control-frame rules in one place instead of at
/// every protocol built on top of a socket.
class Socket {
public:
    using Message = std::function<void(const domain::http::WsFrame &)>;
    using Closed = std::function<void(domain::http::HttpError)>;

    Socket() = default;
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    Socket(Socket &&) = delete;
    Socket &operator=(Socket &&) = delete;
    virtual ~Socket() = default;

    virtual void onMessage(Message handler) = 0;
    virtual void onClose(Closed handler) = 0;
    virtual void sendBinary(std::string_view payload) = 0;
    /// Start the closing handshake. A second call is a no-op.
    virtual void close(std::string_view reason) = 0;
    [[nodiscard]] virtual bool isOpen() const noexcept = 0;
};

class Client {
public:
    Client() = default;
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;
    Client(Client &&) = delete;
    Client &operator=(Client &&) = delete;
    virtual ~Client() = default;

    /// The configuration this client was built with.
    [[nodiscard]] virtual const domain::http::ClientConfig &config() const noexcept = 0;

    [[nodiscard]] virtual std::expected<std::shared_ptr<Ticket>, domain::http::HttpError>
    submit(domain::http::Request request, Completion done) = 0;

    /// Same as `submit`, but the body is handed over as it arrives instead of
    /// being accumulated into the completion.
    [[nodiscard]] virtual std::expected<std::shared_ptr<Ticket>, domain::http::HttpError>
    submitStreaming(domain::http::Request request, StreamHandler handler, Completion done) = 0;

    /// Upgrade to a WebSocket. `ready` runs once with the socket and never
    /// with a failed one; `failed` runs instead. Offering a subprotocol the
    /// peer does not echo is a failure, not a silent downgrade.
    using SocketReady = std::function<void(std::shared_ptr<Socket>)>;
    using SocketFailed = std::function<void(domain::http::HttpError)>;

    virtual void openSocket(domain::http::Url url, std::string subprotocol, SocketReady ready,
                            SocketFailed failed) = 0;

    [[nodiscard]] virtual ClientStats stats() const noexcept = 0;
};

} // namespace antares::ports::http
