#pragma once

#include <cstdint>

namespace antares::domain::k8s {

/// What a scope publishes about itself (FR-004, FR-007).
struct ScopeState {
    /// A 410 expired the watch. The rows stay visible until the relist that
    /// follows completes.
    bool stale = false;
    /// An object was refused because a ceiling was reached, so the row set is
    /// no longer the whole server answer.
    bool truncated = false;
    /// How many objects were refused. Monotonic for the life of the scope.
    std::uint64_t refused = 0;
    /// How many deltas named a uid the scope does not hold. That is what a
    /// refusal looks like from the stream, so it is counted rather than failed
    /// (FR-006).
    std::uint64_t unknown = 0;
};

} // namespace antares::domain::k8s
