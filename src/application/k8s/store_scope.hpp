#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "domain/k8s/entry.hpp"
#include "domain/k8s/scope.hpp"
#include "domain/k8s/scope_state.hpp"
#include "domain/k8s/uid.hpp"

namespace antares::application::k8s {

/// One open scope: the rows in arrival order, the uid index, and what the scope
/// publishes about itself (FR-001, FR-007).
///
/// A removal tombstones its row and the batch compacts once at the end, so an
/// index stated to a view is never renumbered mid-batch. The scope owns no
/// thread: it is confined to the store's thread by construction (FR-009).
class StoreScope {
public:
    explicit StoreScope(domain::k8s::Scope scope);

    StoreScope(const StoreScope &) = delete;
    StoreScope &operator=(const StoreScope &) = delete;
    StoreScope(StoreScope &&) = delete;
    StoreScope &operator=(StoreScope &&) = delete;
    ~StoreScope() = default;

    [[nodiscard]] const domain::k8s::Scope &scope() const noexcept { return scope_; }
    [[nodiscard]] const domain::k8s::ScopeState &state() const noexcept { return state_; }
    [[nodiscard]] domain::k8s::ScopeState &state() noexcept { return state_; }

    /// Every row slot, tombstones included, in arrival order.
    [[nodiscard]] std::span<const std::optional<domain::k8s::Entry>> rows() const noexcept
    {
        return rows_;
    }

    /// Live entries, tombstones excluded.
    [[nodiscard]] std::size_t size() const noexcept { return live_; }

    /// The entry in a live row.
    [[nodiscard]] const domain::k8s::Entry &at(std::size_t row) const;

    [[nodiscard]] const domain::k8s::Entry *find(const domain::k8s::Uid &uid) const noexcept;
    [[nodiscard]] std::optional<std::size_t> rowOf(const domain::k8s::Uid &uid) const noexcept;

    /// Append a row and return its slot. Only ever called for a new uid.
    std::size_t append(domain::k8s::Entry entry);

    /// Overwrite a live row in place.
    void replace(std::size_t row, domain::k8s::Entry entry);

    /// Tombstone a live row.
    void erase(std::size_t row);

    /// Drop every tombstone and rebuild the index. Once per batch.
    void compact();

    /// A 410 expired the watch; the rows stay until the relist finishes.
    void markStale() noexcept { state_.stale = true; }

    /// Open a relist epoch and return it. The seen-set spans the pages, so a
    /// second relist cannot see a first one's marks.
    [[nodiscard]] std::uint64_t beginEpoch() noexcept;

    [[nodiscard]] bool epochIs(std::uint64_t epoch) const noexcept
    {
        return relisting_ && epoch_ == epoch;
    }

    void markSeen(const domain::k8s::Uid &uid);
    [[nodiscard]] bool seen(const domain::k8s::Uid &uid) const;
    void endEpoch() noexcept;

private:
    domain::k8s::Scope scope_;
    std::vector<std::optional<domain::k8s::Entry>> rows_;
    std::unordered_map<domain::k8s::Uid, std::size_t, domain::k8s::UidHash> index_;
    domain::k8s::ScopeState state_;
    std::unordered_set<domain::k8s::Uid, domain::k8s::UidHash> seen_;
    std::uint64_t epoch_ = 0;
    std::size_t live_ = 0;
    bool relisting_ = false;
};

} // namespace antares::application::k8s
