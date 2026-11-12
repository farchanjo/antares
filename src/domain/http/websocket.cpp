#include "domain/http/websocket.hpp"

#include <array>

#include <expected>

namespace antares::domain::http {
namespace {

constexpr std::uint8_t MASK_BIT = 0x80;
constexpr std::uint8_t FIN_BIT = 0x80;
constexpr std::uint8_t RSV_MASK = 0x70;
constexpr std::uint8_t OPCODE_MASK = 0x0F;
constexpr std::size_t MAX_CONTROL_PAYLOAD = 125;

std::array<std::byte, 4> maskBytes(std::uint32_t key)
{
    return {static_cast<std::byte>((key >> 24) & 0xFF), static_cast<std::byte>((key >> 16) & 0xFF),
            static_cast<std::byte>((key >> 8) & 0xFF), static_cast<std::byte>(key & 0xFF)};
}

} // namespace

WsCodec::WsCodec(Bytes ceiling)
    : ceiling_(ceiling)
{
}

std::string WsCodec::encode(WsOpcode opcode, std::string_view payload, std::uint32_t key, bool fin)
{
    std::string out;
    out.reserve(payload.size() + 14);

    out.push_back(static_cast<char>((fin ? FIN_BIT : 0) | static_cast<std::uint8_t>(opcode)));

    const std::size_t size = payload.size();
    if (size < 126) {
        out.push_back(static_cast<char>(MASK_BIT | static_cast<std::uint8_t>(size)));
    } else if (size <= 0xFFFF) {
        out.push_back(static_cast<char>(MASK_BIT | 126));
        out.push_back(static_cast<char>((size >> 8) & 0xFF));
        out.push_back(static_cast<char>(size & 0xFF));
    } else {
        out.push_back(static_cast<char>(MASK_BIT | 127));
        for (int shift = 56; shift >= 0; shift -= 8) {
            out.push_back(static_cast<char>((static_cast<std::uint64_t>(size) >> shift) & 0xFF));
        }
    }

    const auto mask = maskBytes(key);
    out.append(reinterpret_cast<const char *>(mask.data()), mask.size());
    for (std::size_t index = 0; index < size; ++index) {
        out.push_back(static_cast<char>(static_cast<std::uint8_t>(payload[index]) ^
                                        static_cast<std::uint8_t>(mask[index % 4])));
    }
    return out;
}

std::expected<bool, HttpError> WsCodec::takeFrame()
{
    if (buffer_.size() < 2) {
        return false;
    }

    const auto first = static_cast<std::uint8_t>(buffer_[0]);
    const auto second = static_cast<std::uint8_t>(buffer_[1]);
    const bool masked = (second & MASK_BIT) != 0;
    std::size_t length = second & 0x7F;
    std::size_t offset = 2;

    if (length == 126) {
        if (buffer_.size() < offset + 2) {
            return false;
        }
        length = (static_cast<std::size_t>(static_cast<std::uint8_t>(buffer_[offset])) << 8) |
                 static_cast<std::uint8_t>(buffer_[offset + 1]);
        offset += 2;
    } else if (length == 127) {
        if (buffer_.size() < offset + 8) {
            return false;
        }
        length = 0;
        for (int index = 0; index < 8; ++index) {
            length = (length << 8) | static_cast<std::uint8_t>(buffer_[offset + index]);
        }
        offset += 8;
    }

    if (length > ceiling_.count()) {
        return std::unexpected(HttpError::bodyCeiling);
    }

    std::array<std::byte, 4> mask{};
    if (masked) {
        if (buffer_.size() < offset + 4) {
            return false;
        }
        for (std::size_t index = 0; index < 4; ++index) {
            mask[index] = static_cast<std::byte>(buffer_[offset + index]);
        }
        offset += 4;
    }

    if (buffer_.size() < offset + length) {
        return false;
    }

    WsFrame frame;
    frame.fin = (first & FIN_BIT) != 0;
    frame.opcode = static_cast<WsOpcode>(first & OPCODE_MASK);
    frame.payload.assign(buffer_, offset, length);

    if (masked) {
        for (std::size_t index = 0; index < length; ++index) {
            frame.payload[index] = static_cast<char>(static_cast<std::uint8_t>(frame.payload[index]) ^
                                                     static_cast<std::uint8_t>(mask[index % 4]));
        }
    }

    // RFC 6455 §5.2: RSV1-3 are zero unless an extension claimed them, and no
    // extension is ever negotiated here.
    if ((first & RSV_MASK) != 0) {
        return std::unexpected(HttpError::protocol);
    }

    const auto code = static_cast<std::uint8_t>(frame.opcode);
    const bool control = (code & 0x08) != 0;
    if (control && length > MAX_CONTROL_PAYLOAD) {
        return std::unexpected(HttpError::protocol);
    }
    // §5.5: control frames are never fragmented.
    if (control && !frame.fin) {
        return std::unexpected(HttpError::protocol);
    }
    // §5.2: 0x3-0x7 and 0xB-0xF are reserved, so nothing would interpret them.
    if (code > 0x2 && !control && code != 0x0) {
        return std::unexpected(HttpError::protocol);
    }
    if (control && code > 0xA) {
        return std::unexpected(HttpError::protocol);
    }
    if (frame.opcode == WsOpcode::close) {
        closed_ = true;
    }

    buffer_.erase(0, offset + length);
    last_ = std::move(frame);
    return true;
}

std::expected<std::vector<WsFrame>, HttpError> WsCodec::feed(const std::byte *data,
                                                             std::size_t length)
{
    if (buffer_.size() + length > ceiling_.count() * 4) {
        return std::unexpected(HttpError::bodyCeiling);
    }
    buffer_.append(reinterpret_cast<const char *>(data), length);

    std::vector<WsFrame> frames;
    while (true) {
        auto taken = takeFrame();
        if (!taken) {
            return std::unexpected(taken.error());
        }
        if (!*taken) {
            return frames;
        }
        frames.push_back(std::move(last_));
    }
}

} // namespace antares::domain::http
