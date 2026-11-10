#include "adapters/http/websocket.hpp"

#include <algorithm>
#include <array>
#include <cctype>

#include <openssl/rand.h>
#include <openssl/sha.h>

#include <chrono>

namespace antares::adapters::http {

namespace {

/// RFC 6455 §1.3: the GUID a server appends before hashing the client's key.
constexpr std::string_view acceptGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

[[nodiscard]] std::string base64(std::string_view bytes)
{
    // 16 bytes in, 24 characters out, plus the terminator.
    std::string encoded(4 * ((bytes.size() + 2) / 3) + 1, '\0');
    const int written = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(encoded.data()),
                                        reinterpret_cast<const unsigned char *>(bytes.data()),
                                        static_cast<int>(bytes.size()));
    if (written <= 0) {
        return {};
    }
    encoded.resize(static_cast<std::size_t>(written));
    return encoded;
}

/// Case-insensitive search for a header value in a response head.
[[nodiscard]] std::string_view headerValue(std::string_view head, std::string_view name)
{
    std::string lowered{head};
    std::ranges::transform(lowered, lowered.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::string needle{name};
    needle += ':';
    const std::size_t at = lowered.find(needle);
    if (at == std::string::npos) {
        return {};
    }
    std::size_t start = at + needle.size();
    const std::size_t stop = head.find("\r\n", start);
    if (stop == std::string::npos) {
        return {};
    }
    while (start < stop && (head[start] == ' ' || head[start] == '\t')) {
        ++start;
    }
    return head.substr(start, stop - start);
}

} // namespace

std::string WebSocket::newKey()
{
    std::array<unsigned char, 16> random{};
    if (RAND_bytes(random.data(), static_cast<int>(random.size())) != 1) {
        // A predictable key is worse than none: the server's accept would be
        // forgeable, so the upgrade fails rather than guessing.
        return {};
    }
    return base64(std::string_view{reinterpret_cast<const char *>(random.data()), random.size()});
}

std::string WebSocket::acceptFor(std::string_view key)
{
    std::string material{key};
    material += acceptGuid;

    std::array<unsigned char, SHA_DIGEST_LENGTH> digest{};
    const unsigned char *data = reinterpret_cast<const unsigned char *>(material.data());
    (void)SHA1(data, material.size(), digest.data());
    return base64(std::string_view{reinterpret_cast<const char *>(digest.data()), digest.size()});
}

bool WebSocket::verifyHandshake(std::string_view head, std::string_view key,
                                std::string_view subprotocol)
{
    if (key.empty()) {
        return false;
    }
    if (headerValue(head, "sec-websocket-accept") != acceptFor(key)) {
        return false;
    }
    if (subprotocol.empty()) {
        return true;
    }
    // The server picks one of the offers; anything else is a downgrade we did
    // not ask for.
    const std::string_view chosen = headerValue(head, "sec-websocket-protocol");
    return !chosen.empty() && chosen == subprotocol;
}


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

void WebSocket::armCloseTimer()
{
    const std::uint64_t generation = ++closeGeneration_;
    auto self = shared_from_this();
    (void)connection_->engine().submitTimer(std::chrono::seconds{5}, [self, generation] {
        if (generation == self->closeGeneration_) {
            self->finish(domain::http::HttpError::timeout);
        }
    });
}

void WebSocket::close(std::string_view reason)
{
    if (!open_ || closing_) {
        return;
    }
    closing_ = true;
    send(domain::http::WsOpcode::close, reason);
    armCloseTimer();
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
        if (frame.opcode == domain::http::WsOpcode::close) {
            // Peer closed: echo once if this side has not started, then finish.
            if (!closing_) {
                closing_ = true;
                send(domain::http::WsOpcode::close, frame.payload);
            }
            finish(domain::http::HttpError::closed);
            return;
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
