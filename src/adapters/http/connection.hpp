#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <functional>
#include <memory>
#include <string>
#include <deque>
#include <vector>

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
using DataHandler = std::function<void(const std::byte *, std::size_t)>;

/// One connection: TCP connect, TLS handshake, ALPN, then a byte pipe.
///
/// TLS never touches the socket directly. Records go through memory BIOs, so
/// the engine keeps owning readiness and a non-blocking handshake is just
/// another state the loop drives.
class Connection : public std::enable_shared_from_this<Connection> {
public:
    enum class State : std::uint8_t { idle, connecting, handshaking, ready, closed };

    /// What ALPN negotiated. `none` means the URL was plaintext.
    enum class Protocol : std::uint8_t { none, http1, http2 };

    Connection(ports::io::IoEngine &engine, const TlsContext &tls);
    ~Connection();

    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    void open(const domain::http::Url &url, Ready ready, Failure failure);

    [[nodiscard]] Protocol protocol() const noexcept { return protocol_; }
    [[nodiscard]] bool isHttp2() const noexcept { return protocol_ == Protocol::http2; }
    [[nodiscard]] bool isReady() const noexcept { return state_ == State::ready; }
    [[nodiscard]] const domain::http::Url &url() const noexcept { return url_; }
    [[nodiscard]] ports::io::IoEngine &engine() const noexcept { return engine_; }

    /// Decrypted bytes the peer sent. Replaces any previous handler.
    void onData(DataHandler handler) { handler_ = std::move(handler); }

    using WriteDone = std::function<void(std::expected<std::size_t, domain::http::HttpError>)>;
    void write(ports::io::Buffer bytes, WriteDone done);

    void close();

private:
    void fail(domain::http::HttpError error);
    void ready();

    void readSocket();
    void writeSocket();
    void onSocketRead(std::expected<ports::io::Buffer, ports::io::IoError> chunk);
    void onSocketWritten(std::expected<std::size_t, ports::io::IoError> wrote);

    void driveHandshake();
    void drainOutgoing();
    void drainPlaintext();

    ports::io::IoEngine &engine_;
    const TlsContext &tls_;
    ports::io::IoHandle handle_;
    SSL *ssl_ = nullptr;
    BIO *incoming_ = nullptr;
    BIO *outgoing_ = nullptr;
    domain::http::Url url_;
    State state_ = State::idle;
    Protocol protocol_ = Protocol::none;
    std::string plain_;
    std::deque<WriteDone> writeQueue_;
    DataHandler handler_;
    Ready ready_;
    Failure failure_;
    bool writing_ = false;
};

} // namespace antares::adapters::http
