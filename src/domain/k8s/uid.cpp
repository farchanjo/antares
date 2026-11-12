#include "domain/k8s/uid.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <string>

namespace antares::domain::k8s {

namespace {

constexpr std::string_view HEX_DIGITS = "0123456789abcdef";

/// The value of one hexadecimal digit, or -1.
[[nodiscard]] int hexValue(char digit) noexcept
{
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + 10;
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + 10;
    }
    return -1;
}

[[nodiscard]] bool isDash(std::size_t index) noexcept
{
    return index == 8 || index == 13 || index == 18 || index == 23;
}

} // namespace

Uid Uid::ofName(std::string_view name) noexcept
{
    // FNV-1a, twice with different offsets, so the sixteen bytes spread over the
    // whole name instead of repeating one value.
    Uid out;
    for (std::size_t pass = 0; pass < 2; ++pass) {
        std::uint64_t hash = pass == 0 ? 1469598103934665603ULL : 1099511628211ULL;
        for (const char at : name) {
            hash ^= static_cast<std::uint8_t>(at);
            hash *= 1099511628211ULL;
        }
        for (std::size_t at = 0; at < 8; ++at) {
            out.bytes_[(pass * 8) + at] = static_cast<std::byte>((hash >> (at * 8)) & 0xFF);
        }
    }
    return out;
}

std::expected<Uid, StoreError> Uid::parse(std::string_view text) noexcept
{
    Uid parsed;
    std::size_t digits = 0;

    if (text.size() == textLength) {
        for (std::size_t index = 0; index < text.size(); ++index) {
            if (isDash(index)) {
                if (text[index] != '-') {
                    return std::unexpected(StoreError::invalidUid);
                }
                continue;
            }
            const int value = hexValue(text[index]);
            if (value < 0) {
                return std::unexpected(StoreError::invalidUid);
            }
            const auto shift = static_cast<std::size_t>(value);
            parsed.bytes_[digits / 2] |= static_cast<std::byte>(
                static_cast<unsigned>(digits % 2 == 0 ? shift << 4 : shift));
            ++digits;
        }
        return parsed;
    }

    if (text.size() != byteLength * 2) {
        return std::unexpected(StoreError::invalidUid);
    }
    for (const char character : text) {
        const int value = hexValue(character);
        if (value < 0) {
            return std::unexpected(StoreError::invalidUid);
        }
        const auto shift = static_cast<std::size_t>(value);
        parsed.bytes_[digits / 2] |= static_cast<std::byte>(
            static_cast<unsigned>(digits % 2 == 0 ? shift << 4 : shift));
        ++digits;
    }
    return parsed;
}

std::string Uid::text() const
{
    std::string rendered;
    rendered.reserve(textLength);
    for (std::size_t index = 0; index < bytes_.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            rendered.push_back('-');
        }
        const auto value = static_cast<unsigned char>(bytes_[index]);
        rendered.push_back(HEX_DIGITS[value >> 4]);
        rendered.push_back(HEX_DIGITS[value & 0x0FU]);
    }
    return rendered;
}

bool Uid::empty() const noexcept
{
    return std::ranges::all_of(bytes_, [](std::byte value) { return value == std::byte{0}; });
}

std::size_t UidHash::operator()(const Uid &uid) const noexcept
{
    // FNV-1a over the sixteen bytes: the key is fixed width, so a simple fold
    // is enough and it stays an allocator-free constant.
    std::size_t hash = 1469598103934665603ULL;
    for (const std::byte value : uid.bytes()) {
        hash ^= static_cast<std::size_t>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace antares::domain::k8s
