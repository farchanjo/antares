#include "domain/k8s/store_metrics.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace antares::domain::k8s {

std::size_t StoreMetrics::indexOf(std::string_view name, ScopeKind kind) noexcept
{
    const std::size_t base = name == "store.entries" ? 0 : 2;
    return base + (kind == ScopeKind::cluster ? 0 : 1);
}

void StoreMetrics::observeEntries(ScopeKind kind, std::uint64_t value) noexcept
{
    samples_[indexOf("store.entries", kind)].value = value;
}

void StoreMetrics::observeRefused(ScopeKind kind, std::uint64_t value) noexcept
{
    samples_[indexOf("store.refused", kind)].value = value;
}

std::uint64_t StoreMetrics::entries(ScopeKind kind) const noexcept
{
    return samples_[indexOf("store.entries", kind)].value;
}

std::uint64_t StoreMetrics::refused(ScopeKind kind) const noexcept
{
    return samples_[indexOf("store.refused", kind)].value;
}

} // namespace antares::domain::k8s
