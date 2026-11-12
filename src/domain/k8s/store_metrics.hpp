#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "domain/k8s/scope.hpp"

namespace antares::domain::k8s {

/// One sample of a metric with its one label.
struct StoreSample {
    std::string_view name;
    std::string_view scopeKind;
    std::uint64_t value = 0;
};

/// The two bounded-label store metrics (FR-004, D-15).
///
/// `scope_kind` has exactly two values, so the cardinality is fixed at two per
/// metric and the sample array is a fixed four. Per-object bytes are asserted by
/// the suite instead of labelled, because an object is unbounded.
class StoreMetrics {
public:
    static constexpr std::size_t sampleCount = 4;

    void observeEntries(ScopeKind kind, std::uint64_t value) noexcept;
    void observeRefused(ScopeKind kind, std::uint64_t value) noexcept;

    [[nodiscard]] std::uint64_t entries(ScopeKind kind) const noexcept;
    [[nodiscard]] std::uint64_t refused(ScopeKind kind) const noexcept;

    /// Every sample, in `entries` then `refused` order, both by scope kind.
    [[nodiscard]] std::span<const StoreSample> samples() const noexcept { return samples_; }

private:
    [[nodiscard]] static std::size_t indexOf(std::string_view name, ScopeKind kind) noexcept;

    std::array<StoreSample, sampleCount> samples_{
        StoreSample{"store.entries", "cluster", 0},
        StoreSample{"store.entries", "namespace", 0},
        StoreSample{"store.refused", "cluster", 0},
        StoreSample{"store.refused", "namespace", 0},
    };
};

} // namespace antares::domain::k8s
