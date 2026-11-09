#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace antares::domain::k8s {

/// The reasons an API server may report in a `Status` document. Closed on
/// purpose: a caller switching on it must be told when a new case appears.
enum class Reason : std::uint8_t {
    notFound,
    alreadyExists,
    conflict,
    forbidden,
    unauthorized,
    invalid,
    timeout,
    tooManyRequests,
    internalError,
    serviceUnavailable,
    expired,
    unexpected,
};

[[nodiscard]] constexpr std::string_view reasonName(Reason reason) noexcept
{
    switch (reason) {
    case Reason::notFound:
        return "NotFound";
    case Reason::alreadyExists:
        return "AlreadyExists";
    case Reason::conflict:
        return "Conflict";
    case Reason::forbidden:
        return "Forbidden";
    case Reason::unauthorized:
        return "Unauthorized";
    case Reason::invalid:
        return "Invalid";
    case Reason::timeout:
        return "Timeout";
    case Reason::tooManyRequests:
        return "TooManyRequests";
    case Reason::internalError:
        return "InternalError";
    case Reason::serviceUnavailable:
        return "ServiceUnavailable";
    case Reason::expired:
        return "Expired";
    case Reason::unexpected:
        return "Unexpected";
    }
    return "Unexpected";
}

/// Map a server reason token to the typed value; an unknown token is
/// `unexpected`, never a guess.
[[nodiscard]] Reason reasonFromToken(std::string_view token) noexcept;

/// True when the failure is worth another attempt (FR-012).
[[nodiscard]] bool isRetryable(Reason reason) noexcept;

/// The server's failure, projected. `message` is bounded so a hostile body
/// cannot grow a log line without limit (FR-005).
struct ApiError {
    Reason reason = Reason::unexpected;
    std::uint16_t code = 0;
    std::string message;

    /// Largest server message kept, in bytes.
    static constexpr std::size_t messageCeiling = 512;

    [[nodiscard]] std::string_view name() const noexcept { return reasonName(reason); }
};

} // namespace antares::domain::k8s
