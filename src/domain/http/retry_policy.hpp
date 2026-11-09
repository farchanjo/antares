#pragma once

#include <cstdint>

#include "domain/units/duration.hpp"

namespace antares::domain::http {

/// When a failed attempt is worth repeating, and how long to wait (FR-015).
///
/// Retries are bounded on purpose: a retry storm on a sick server is worse than
/// a fast failure.
struct RetryPolicy {
    /// Total attempts, the first one included. 1 disables retries.
    std::uint8_t maxAttempts = 3;

    /// Delay before the second attempt; later attempts multiply it.
    Duration baseDelay = std::chrono::milliseconds{100};

    /// Ceiling for the computed delay.
    Duration maxDelay = std::chrono::seconds{2};

    /// Delay multiplier per attempt.
    double factor = 2.0;

    /// Fraction of the delay randomised, 0 disables jitter.
    double jitter = 0.2;

    /// Retry when the connection could not be established.
    bool retryOnConnect = true;

    /// Retry when a deadline expired.
    bool retryOnTimeout = true;

    /// Retry these status codes. 429 and the 5xx family by default.
    std::uint16_t retryOnStatus[4] = {429, 502, 503, 504};

    /// Honour a `Retry-After` header instead of the computed delay.
    bool honorRetryAfter = true;

    /// Retry a method that is not idempotent. Off by default: a POST repeated
    /// after a partial write may duplicate work.
    bool retryNonIdempotent = false;

    [[nodiscard]] bool isRetryableStatus(std::uint16_t status) const noexcept;
};

} // namespace antares::domain::http
