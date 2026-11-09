#pragma once

#include <chrono>

#include "domain/units/duration.hpp"

namespace antares::domain {

/// A point on the monotonic clock. Only differences are meaningful, which is
/// what a deadline needs.
class Instant {
public:
    constexpr Instant() = default;

    [[nodiscard]] static Instant now() noexcept
    {
        Instant instant;
        instant.value_ = std::chrono::steady_clock::now();
        return instant;
    }

    [[nodiscard]] bool isBefore(Instant other) const noexcept { return value_ < other.value_; }

    [[nodiscard]] Instant operator+(Duration span) const noexcept
    {
        Instant result;
        result.value_ = value_ + span.chrono();
        return result;
    }

    friend Duration operator-(Instant later, Instant earlier) noexcept
    {
        return Duration::of(later.value_ - earlier.value_);
    }

    friend auto operator<=>(const Instant &left, const Instant &right) noexcept = default;

private:
    std::chrono::steady_clock::time_point value_{};
};

} // namespace antares::domain
