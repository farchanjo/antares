#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "domain/http/http_error.hpp"
#include "domain/http/request.hpp"
#include "domain/http/response.hpp"
#include "ports/io/io_engine.hpp"

namespace antares::ports::http {

/// Opaque in-flight request. Cancelling is idempotent.
class Ticket {
public:
    virtual ~Ticket() = default;
    virtual void cancel() = 0;
};

struct ClientStats {
    std::uint64_t opened = 0;
    std::uint64_t reused = 0;
    std::uint64_t completed = 0;
    std::uint64_t redirects = 0;
    std::uint64_t tlsFailures = 0;
};

/// Completion runs on the engine thread, exactly once (FR-007).
using Completion =
    std::function<void(std::expected<domain::http::Response, domain::http::HttpError>)>;

/// Async HTTP client. Every operation is submitted to the engine and returns;
/// nothing here blocks (FR-002).
class Client {
public:
    Client() = default;
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;
    Client(Client &&) = delete;
    Client &operator=(Client &&) = delete;
    virtual ~Client() = default;

    [[nodiscard]] virtual std::expected<std::shared_ptr<Ticket>, domain::http::HttpError>
    submit(domain::http::Request request, Completion done) = 0;

    [[nodiscard]] virtual ClientStats stats() const noexcept = 0;
};

} // namespace antares::ports::http
