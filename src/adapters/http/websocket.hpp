#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "adapters/http/connection.hpp"
#include "domain/http/http_error.hpp"
#include "domain/http/websocket.hpp"
#include "ports/http/client.hpp"

namespace antares::adapters::http {

/// An established WebSocket. Built only by `HttpClient::openWebSocket`, after
/// the 101 (RFC 6455 §4).
///
/// Frames are queued through the connection; pings are answered by the codec
/// owner, not here.
class WebSocket final : public ports::http::Socket,
                        public std::enable_shared_from_this<WebSocket> {
public:
    using Message = std::function<void(const domain::http::WsFrame &)>;
    using Closed = std::function<void(domain::http::HttpError)>;

    WebSocket(std::shared_ptr<Connection> connection, domain::Bytes ceiling);

    /// A fresh `Sec-WebSocket-Key` (RFC 6455 §4.1): sixteen random bytes, base64.
    /// The server echoes a digest of it, so a predictable key would defeat the
    /// only proof that the peer actually spoke WebSocket.
    [[nodiscard]] static std::string newKey();

    /// The `Sec-WebSocket-Accept` value a correct server returns for `key`:
    /// base64 of SHA-1 over the key concatenated with the RFC's GUID.
    [[nodiscard]] static std::string acceptFor(std::string_view key);

    /// Check a 101 response head against the key we sent. A missing or wrong
    /// accept means the peer is not the server we think, so the upgrade is
    /// refused rather than trusted. When `subprotocol` is not empty the server
    /// must echo one, or the caller would speak a protocol nobody agreed to.
    [[nodiscard]] static bool verifyHandshake(std::string_view head, std::string_view key,
                                              std::string_view subprotocol);

    /// Turn on permessage-deflate (RFC 7692) for both directions.
    void enableDeflate();

    void onMessage(Message handler) { message_ = std::move(handler); }
    void onClose(Closed handler) { closed_ = std::move(handler); }

    void sendText(std::string_view text);
    void sendBinary(std::string_view payload);
    void sendPing(std::string_view payload);
    /// Starts the closing handshake (RFC 6455 §7.1.2): send close, then wait
    /// for the peer's close before dropping the socket. The timeout closes
    /// unilaterally.
    void close(std::string_view reason);

    [[nodiscard]] bool isOpen() const noexcept { return open_; }

    /// Report a failure without sending anything. A failed upgrade is not a
    /// closing handshake, so writing a close frame there would be noise on a
    /// connection that never became a socket.
    void abort(domain::http::HttpError error) { finish(error); }

    /// Feed decrypted bytes; called by the client's data handler.
    void receive(const std::byte *data, std::size_t length);

private:
    void send(domain::http::WsOpcode opcode, std::string_view payload);
    void finish(domain::http::HttpError error);
    void armCloseTimer();

    std::shared_ptr<Connection> connection_;
    domain::http::WsCodec codec_;
    Message message_;
    /// Fragments of the message being reassembled, and the opcode it opened with.
    std::string pending_;
    domain::http::WsOpcode pendingOpcode_ = domain::http::WsOpcode::text;
    bool fragmented_ = false;
    bool deflate_ = false;
    bool pendingCompressed_ = false;
    Closed closed_;
    std::uint32_t mask_ = 0x1234ABCDU;
    std::uint64_t closeGeneration_ = 0;
    bool closing_ = false;
    bool open_ = true;
};

} // namespace antares::adapters::http
