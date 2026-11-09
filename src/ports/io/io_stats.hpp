#pragma once

#include <array>
#include <cstdint>

#include "ports/io/io_error.hpp"

namespace antares::ports::io {

/// Failure buckets, one per `IoError` case, so a counter never guesses.
inline constexpr std::uint8_t FAILURE_FAMILIES = 5;

/// Engine counters (FR-001). Read on the engine thread; the snapshot is a copy.
struct EngineStats {
    std::uint64_t submitted = 0;
    std::uint64_t completed = 0;
    std::array<std::uint64_t, FAILURE_FAMILIES> failed{};

    [[nodiscard]] std::uint64_t failures() const noexcept
    {
        std::uint64_t total = 0;
        for (const std::uint64_t count : failed) {
            total += count;
        }
        return total;
    }
};

/// Index a failure into `EngineStats::failed`.
[[nodiscard]] constexpr std::uint8_t failureIndex(IoError error) noexcept
{
    return static_cast<std::uint8_t>(error);
}

} // namespace antares::ports::io
