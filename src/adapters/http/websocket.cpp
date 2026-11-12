#include "adapters/http/websocket.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>

#include <openssl/rand.h>
#include <openssl/sha.h>

#include <chrono>

namespace antares::adapters::http {

namespace {

/// RFC 6455 §1.3: the GUID a server appends before hashing the client's key.
constexpr std::string_view ACCEPT_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

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
    // The header being looked for is often the last one before the blank line,
    // so an absent CRLF means "runs to the end of the head", not "no value".
    const std::size_t found = head.find("\r\n", start);
    const std::size_t stop = found == std::string_view::npos ? head.size() : found;
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
    material += ACCEPT_GUID;

    std::array<unsigned char, SHA_DIGEST_LENGTH> digest{};
    const auto* data = reinterpret_cast<const unsigned char*>(material.data());
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

void WebSocket::enableDeflate()
{
    deflate_ = true;
    codec_.allowDeflate(true);
}

namespace {

/// Raw deflate, with the four-byte tail RFC 7692 §7.2.1 appends and the receiver
/// strips. A message that grows is sent as it was, so the flag never lies.
[[nodiscard]] bool deflateMessage(std::string_view plain, std::string &coded)
{
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) !=
        Z_OK) {
        return false;
    }

    coded.assign(compressBound(plain.size()) + 8, '\0');
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(plain.data()));
    stream.avail_in = static_cast<uInt>(plain.size());
    stream.next_out = reinterpret_cast<Bytef *>(coded.data());
    stream.avail_out = static_cast<uInt>(coded.size());

    const int status = deflate(&stream, Z_SYNC_FLUSH);
    const std::size_t written = stream.total_out;
    (void)deflateEnd(&stream);
    if (status != Z_OK && status != Z_STREAM_END) {
        return false;
    }

    coded.resize(written);
    if (written < 4) {
        return false;
    }
    // The tail is the empty stored block that marks the flush.
    coded.resize(written - 4);
    return true;
}

/// Undo `deflateMessage`. The tail is put back before inflating, which is what
/// the peer's compressor left off.
[[nodiscard]] std::expected<std::string, domain::http::HttpError>
inflateMessage(std::string_view coded)
{
    z_stream stream{};
    if (inflateInit2(&stream, -15) != Z_OK) {
        return std::unexpected(domain::http::HttpError::protocol);
    }

    std::string input{coded};
    input.append("\x00\x00\xff\xff", 4);

    std::string out;
    out.resize(input.size() * 4 + 64);
    stream.next_in = reinterpret_cast<Bytef *>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());

    const int status = inflate(&stream, Z_SYNC_FLUSH);
    const std::size_t written = stream.total_out;
    (void)inflateEnd(&stream);
    if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
        return std::unexpected(domain::http::HttpError::protocol);
    }
    out.resize(written);
    return out;
}

} // namespace

void WebSocket::send(domain::http::WsOpcode opcode, std::string_view payload)
{
    if (!open_ || !connection_) {
        return;
    }

    // Control frames are never compressed (§7.2.3).
    const bool compress = deflate_ && opcode != domain::http::WsOpcode::ping &&
                          opcode != domain::http::WsOpcode::pong &&
                          opcode != domain::http::WsOpcode::close;
    std::string coded;
    const bool codedOk = compress && deflateMessage(payload, coded);

    std::string frame = domain::http::WsCodec::encode(
        opcode, codedOk ? std::string_view{coded} : payload, mask_, true, codedOk);
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
    static_cast<void>(
        connection_->engine().submitTimer(std::chrono::seconds{5}, [self, generation] {
            if (generation == self->closeGeneration_) {
                self->finish(domain::http::HttpError::timeout);
            }
        }));
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
        // RFC 6455 §5.4: a fragmented message is one data frame with FIN clear
        // and a real opcode, then continuation frames, closed by FIN. The
        // fragments are concatenated and delivered once, typed by the first.
        if (frame.opcode == domain::http::WsOpcode::continuation) {
            if (!fragmented_) {
                // A continuation with no message open is a protocol error.
                finish(domain::http::HttpError::protocol);
                return;
            }
            pending_ += frame.payload;
            if (!frame.fin) {
                continue;
            }
            fragmented_ = false;
            domain::http::WsFrame whole;
            whole.opcode = pendingOpcode_;
            whole.payload = std::move(pending_);
            pending_.clear();
            if (pendingCompressed_) {
                auto plain = inflateMessage(whole.payload);
                if (!plain) {
                    finish(domain::http::HttpError::protocol);
                    return;
                }
                whole.payload = std::move(*plain);
            }
            if (message_) {
                message_(whole);
            }
            continue;
        }

        if (!frame.fin) {
            fragmented_ = true;
            pendingOpcode_ = frame.opcode;
            pendingCompressed_ = frame.rsv1;
            pending_ = frame.payload;
            continue;
        }
        if (!frame.rsv1) {
            if (message_) {
                message_(frame);
            }
            continue;
        }
        auto plain = inflateMessage(frame.payload);
        if (!plain) {
            finish(domain::http::HttpError::protocol);
            return;
        }
        if (message_) {
            domain::http::WsFrame decoded = frame;
            decoded.payload = std::move(*plain);
            message_(decoded);
        }
    }

    if (codec_.isClosed()) {
        finish(domain::http::HttpError::closed);
    }
}

} // namespace antares::adapters::http
