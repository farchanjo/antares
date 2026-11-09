#pragma once

#include "domain/units/duration.hpp"

namespace antares::domain::http {

using Duration = antares::domain::Duration;

/// Deadlines per stage (FR-012). A zero duration disables that deadline.
struct Timeouts {
    /// TCP connect plus TLS handshake.
    Duration connect = std::chrono::seconds{10};

    /// A single read that makes no progress.
    Duration read = std::chrono::seconds{30};

    /// A single write that makes no progress.
    Duration write = std::chrono::seconds{30};

    /// Whole exchange, redirects included. The backstop.
    Duration request = std::chrono::seconds{120};
};

} // namespace antares::domain::http
