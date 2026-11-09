#include "adapters/http/websocket.hpp"

namespace antares::adapters::http {

WebSocket::WebSocket(std::shared_ptr<Connection> connection, domain::Bytes ceiling)
    : connection_(std::move(connection))
    , codec_(ceiling)
{
}

void WebSocket::send(domain::http::WsOpcode opcode, std::string_view payload)
{
    if (!open_ || !connection_) {
        return;
    }
    std::string frame = domain::http::WsCodec::encode(opcode, payload, mask_);
    mask_ = (mask_ * 1664525U) + 1013904223U;
    connection_->write(
        ports::io::Buffer{reinterpret_cast<const std::byte *>(frame.data()),
                          reinterpret_cast<const std::byte *>(frame.data()) + frame.size()},
        [](WriteOutcome) {});
}

void WebSocket::sendText(std::string_view text)
{
    send(domain::http::WsOpcode::text, text);
}

void WebSocket::sendBinary(std::string_view payload)
{
    send(domain::http::WsOpcode::binary, payload);
}

void WebSocket::sendPing(std::string_view payload)
{
    send(domain::http::WsOpcode::ping, payload);
}

void WebSocket::close(std::string_view reason)
{
    if (!open_) {
        return;
    }
    send(domain::http::WsOpcode::close, reason);
    open_ = false;
}

void WebSocket::finish(domain::http::HttpError error)
{
    if (!open_) {
        return;
    }
    open_ = false;
    auto handler = closed_;
    closed_ = nullptr;
    if (handler) {
        handler(error);
    }
}

void WebSocket::receive(const std::byte *data, std::size_t length)
{
    auto frames = codec_.feed(data, length);
    if (!frames) {
        finish(frames.error());
        return;
    }

    for (const domain::http::WsFrame &frame : *frames) {
        if (frame.opcode == domain::http::WsOpcode::ping) {
            send(domain::http::WsOpcode::pong, frame.payload);
            continue;
        }
        if (frame.opcode == domain::http::WsOpcode::pong) {
            continue;
        }
        if (message_) {
            message_(frame);
        }
    }

    if (codec_.isClosed()) {
        finish(domain::http::HttpError::closed);
    }
}

} // namespace antares::adapters::http
