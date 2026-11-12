#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "domain/http/http_error.hpp"
#include "domain/units/bytes.hpp"

namespace antares::domain::http {

/// RFC 6455 opcodes that matter to a client.
enum class WsOpcode : std::uint8_t { continuation = 0x0, text = 0x1, binary = 0x2, close = 0x8, ping = 0x9, pong = 0xA };

/// One frame, payload already unmasked.
struct WsFrame {
    WsOpcode opcode = WsOpcode::text;
    std::string payload;
    bool fin = true;
    /// Set when the payload is permessage-deflate coded.
    bool rsv1 = false;
};

/// Client-side framing (RFC 6455 §5). Pure: bytes in, frames out.
///
/// A client always masks, so encoding takes a 32-bit key; decoding unmasks with
/// the key the server chose (servers never mask).
class WsCodec {
public:
    explicit WsCodec(Bytes ceiling);

    /// Encode one frame. `key` seeds the mask; a fixed key keeps tests stable.
    [[nodiscard]] static std::string encode(WsOpcode opcode, std::string_view payload,
                                            std::uint32_t key, bool fin = true,
                                            bool rsv1 = false);

    /// Whether a set RSV1 means permessage-deflate (RFC 7692) rather than a
    /// protocol error. Only true once the extension was negotiated.
    void allowDeflate(bool allowed) noexcept { deflateNegotiated_ = allowed; }

    /// Feed received bytes. Returns every frame completed so far.
    [[nodiscard]] std::expected<std::vector<WsFrame>, HttpError> feed(const std::byte *data,
                                                                     std::size_t length);

    [[nodiscard]] bool isClosed() const noexcept { return closed_; }

private:
    [[nodiscard]] std::expected<bool, HttpError> takeFrame();

    std::string buffer_;
    Bytes ceiling_;
    bool closed_ = false;
    bool deflateNegotiated_ = false;
    WsFrame last_;
};

} // namespace antares::domain::http
