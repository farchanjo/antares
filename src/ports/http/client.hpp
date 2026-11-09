#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "domain/http/client_config.hpp"
#include "domain/http/http_error.hpp"
#include "domain/http/request.hpp"
#include "domain/http/response.hpp"
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

    [[nodiscard]] virtual ClientStats stats() const noexcept = 0;
};

} // namespace antares::ports::http
