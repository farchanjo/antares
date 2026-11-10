#pragma once

#include <cstdint>

namespace antares::ports::io {

/// Closed failure set of the port (ADR 0007).
///
/// A case is added only together with a scenario that reaches it. `system`
/// carries no message: the caller inspects `errno` at the boundary and the
/// engine never formats a path or a host into the type.
enum class IoError : std::uint8_t {
    wouldBlock,
    connect,
    closed,
    cancelled,
    tooManyDescriptors,
    /// A name resolution outlived its budget. Distinct from `system` because
    /// the caller can retry a resolver and a retry reads as pointless on a
    /// plain failure.
    timeout,
    system,
};

} // namespace antares::ports::io
