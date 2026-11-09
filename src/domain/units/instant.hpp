#pragma once

#include <chrono>

#include "domain/units/duration.hpp"

namespace antares::domain {

/// Native monotonic time point. Only differences are meaningful, which is what a
/// deadline needs; `steady_clock` cannot go backwards.
using Instant = std::chrono::steady_clock::time_point;

[[nodiscard]] inline Instant afterFromNow(Duration span) noexcept
{
    return std::chrono::steady_clock::now() + span;
}

[[nodiscard]] inline bool hasPassed(Instant deadline) noexcept
{
    return std::chrono::steady_clock::now() >= deadline;
}

} // namespace antares::domain
