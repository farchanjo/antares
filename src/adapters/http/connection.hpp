#pragma once

#include <cstdint>
#include <functional>
#include <expected>
#include <memory>
#include <string>

#include <openssl/ssl.h>

#include "domain/http/http_error.hpp"
#include "domain/http/url.hpp"
#include "domain/units/bytes.hpp"
#include "ports/io/io_engine.hpp"
#include "ports/io/io_handle.hpp"

namespace antares::adapters::http {

class TlsContext;

using Failure = std::function<void(domain::http::HttpError)>;
using Ready = std::function<void()>;

/// One connection's state machine: TCP connect, TLS handshake, ALPN selection.
/// Every step is one engine operation, so nothing here blocks (FR-003).
///
/// Only the engine thread advances a connection. No mutex guards the state.
class Connection : public std::enable_shared_from_this<Connection> {
public:
    enum class State : std::uint8_t { idle, connecting, handshaking, ready, closed };

    Connection(ports::io::IoEngine &engine, const TlsContext &tls);
    ~Connection();

    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    /// Resolve, connect, handshake, negotiate. Calls `ready` or `failure` once.
    void open(const domain::http::Url &url, Ready ready, Failure failure);

    /// True when the negotiated protocol is HTTP/2.
    [[nodiscard]] bool isHttp2() const noexcept { return http2_; }
    [[nodiscard]] bool isReady() const noexcept { return state_ == State::ready; }
    [[nodiscard]] const domain::http::Url &url() const noexcept { return url_; }

    /// Write the whole buffer; the callback reports the byte count or a failure.
    using WriteDone = std::function<void(std::expected<std::size_t, domain::http::HttpError>)>;
    using ReadDone = std::function<void(std::expected<ports::io::Buffer, domain::http::HttpError>)>;

    void write(ports::io::Buffer bytes, WriteDone done);
    void read(domain::Bytes maxBytes, ReadDone done);
    void close();

private:
    void fail(domain::http::HttpError error);
    void afterConnected();
    void afterHandshake(int result);
    void resolve(const std::string &host, std::uint16_t port);

    ports::io::IoEngine &engine_;
    const TlsContext &tls_;
    ports::io::IoHandle handle_;
    SSL *ssl_ = nullptr;
    domain::http::Url url_;
    State state_ = State::idle;
    bool http2_ = false;
    Failure failure_;
};

} // namespace antares::adapters::http
