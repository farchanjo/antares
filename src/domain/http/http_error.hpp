#pragma once

#include <cstdint>
#include <string_view>

namespace antares::domain::http {

/// Every failure the client reports. Closed on purpose: a caller switching on
/// it must be told when a new case appears.
enum class HttpError : std::uint8_t {
    connect,
    refused,
    tls,
    alpn,
    timeout,
    cancelled,
    reentrant,
    tooManyRedirects,
    headerCeiling,
    bodyCeiling,
    streamCeiling,
    protocol,
    closed,
};

[[nodiscard]] constexpr std::string_view describe(HttpError error) noexcept
{
    switch (error) {
    case HttpError::connect:
        return "connect failed";
    case HttpError::refused:
        return "refused by the peer";
    case HttpError::tls:
        return "tls handshake failed";
    case HttpError::alpn:
        return "no acceptable protocol";
    case HttpError::timeout:
        return "timed out";
    case HttpError::cancelled:
        return "cancelled";
    case HttpError::reentrant:
        return "sync call on the engine thread";
    case HttpError::tooManyRedirects:
        return "redirect limit reached";
    case HttpError::headerCeiling:
        return "header ceiling exceeded";
    case HttpError::bodyCeiling:
        return "body ceiling exceeded";
    case HttpError::streamCeiling:
        return "stream ceiling exceeded";
    case HttpError::protocol:
        return "protocol violation";
    case HttpError::closed:
        return "connection closed";
    }
    return "unknown";
}

} // namespace antares::domain::http
