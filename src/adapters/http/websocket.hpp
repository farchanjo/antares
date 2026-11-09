#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "adapters/http/connection.hpp"
#include "domain/http/http_error.hpp"
#include "domain/http/websocket.hpp"

namespace antares::adapters::http {

/// An established WebSocket. Built only by `HttpClient::openWebSocket`, after
/// the 101 (RFC 6455 §4).
///
/// Frames are queued through the connection; pings are answered by the codec
/// owner, not here.
class WebSocket : public std::enable_shared_from_this<WebSocket> {
public:
    using Message = std::function<void(const domain::http::WsFrame &)>;
    using Closed = std::function<void(domain::http::HttpError)>;

    WebSocket(std::shared_ptr<Connection> connection, domain::Bytes ceiling);

    void onMessage(Message handler) { message_ = std::move(handler); }
    void onClose(Closed handler) { closed_ = std::move(handler); }

    void sendText(std::string_view text);
    void sendBinary(std::string_view payload);
    void sendPing(std::string_view payload);
    /// Starts the closing handshake (RFC 6455 §7.1.2): send close, then wait
    /// for the peer's close before dropping the socket. The timeout closes
    /// unilaterally.
    void close(std::string_view reason = {});

    [[nodiscard]] bool isOpen() const noexcept { return open_; }

    /// Feed decrypted bytes; called by the client's data handler.
    void receive(const std::byte *data, std::size_t length);

private:
    void send(domain::http::WsOpcode opcode, std::string_view payload);
    void finish(domain::http::HttpError error);
    void armCloseTimer();

    std::shared_ptr<Connection> connection_;
    domain::http::WsCodec codec_;
    Message message_;
    Closed closed_;
    std::uint32_t mask_ = 0x1234ABCDU;
    std::uint64_t closeGeneration_ = 0;
    bool closing_ = false;
    bool open_ = true;
};

} // namespace antares::adapters::http
