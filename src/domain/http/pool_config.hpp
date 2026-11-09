#pragma once

#include <cstdint>

namespace antares::domain::http {

/// Connection reuse bounds (FR-011). Every field is a ceiling, never a target.
struct PoolConfig {
    /// Idle connections kept per authority.
    std::uint16_t maxIdlePerHost = 4;

    /// Idle connections kept across every authority.
    std::uint16_t maxIdleTotal = 32;

    /// How long an idle connection survives before it is closed.
    std::uint32_t idleTimeoutMs = 15'000;
};

} // namespace antares::domain::http
