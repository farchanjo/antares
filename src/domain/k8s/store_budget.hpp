#pragma once

#include <cstdint>

namespace antares::domain::k8s {

/// The ceiling shape of `event-store-budget.cue`, as one value.
///
/// Retention is bounded by admission (FR-004): the store refuses past a ceiling
/// instead of evicting, so the row set only ever grows until the ceiling and
/// then says so. `fits()` is the budget proof in code — with the defaults it
/// resolves to 104 497 152 bytes inside the declared 104 857 600.
struct StoreBudget {
    /// Entries one scope admits.
    std::uint32_t maxEntries = 50'000;
    /// Label pairs one entry keeps.
    std::uint32_t labelEntries = 16;
    /// Label bytes one entry keeps, key and value summed.
    std::uint32_t labelBytes = 256;
    /// Per-scope overhead the byte budget charges whether or not it is used.
    std::uint32_t fixedBytes = 65'536;
    /// Scopes the store opens at once.
    std::uint32_t maxScopes = 32;
    /// Entries the whole store admits, all scopes together.
    std::uint64_t maxTotalEntries = 100'000;
    /// Declared bytes the store may retain.
    std::uint64_t maxBytes = 104'857'600;
    /// The fixture entry ceiling the suite asserts (FR-003).
    std::uint32_t typicalEntryBytes = 512;
    /// The ceiling any admitted entry is held to (FR-003).
    std::uint32_t worstEntryBytes = 1'024;

    [[nodiscard]] constexpr std::uint64_t worstCaseBytes() const noexcept
    {
        return maxTotalEntries * worstEntryBytes +
               static_cast<std::uint64_t>(maxScopes) * fixedBytes;
    }

    /// The two inequations of `#StoreBudget`, in code.
    [[nodiscard]] constexpr bool fits() const noexcept
    {
        return maxEntries <= maxTotalEntries && worstCaseBytes() <= maxBytes;
    }
};

} // namespace antares::domain::k8s
