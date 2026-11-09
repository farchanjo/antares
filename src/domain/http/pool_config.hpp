#pragma once

#include <cstdint>

#include "domain/units/duration.hpp"

namespace antares::domain::http {

using Duration = antares::domain::Duration;

/// Connection reuse bounds (FR-011). Every field is a ceiling, never a target.
struct PoolConfig {
    /// Idle connections kept per authority.
    std::uint16_t maxIdlePerHost = 4;

    /// Idle connections kept across every authority.
    std::uint16_t maxIdleTotal = 32;

    /// How long an idle connection survives before it is closed.
    Duration idleTimeout = std::chrono::seconds{15};
};

} // namespace antares::domain::http
