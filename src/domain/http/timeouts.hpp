#pragma once

#include <cstdint>

namespace antares::domain::http {

/// Deadlines per stage (FR-012). Zero disables that deadline.
struct Timeouts {
    /// TCP connect plus TLS handshake.
    std::uint32_t connectMs = 10'000;

    /// A single read that makes no progress.
    std::uint32_t readMs = 30'000;

    /// A single write that makes no progress.
    std::uint32_t writeMs = 30'000;

    /// Whole exchange, redirects included. The backstop.
    std::uint32_t requestMs = 120'000;
};

} // namespace antares::domain::http
