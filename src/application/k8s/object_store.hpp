#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <unordered_map>

#include "application/k8s/store_scope.hpp"
#include "domain/k8s/change_set.hpp"
#include "domain/k8s/entry.hpp"
#include "domain/k8s/object_key.hpp"
#include "domain/k8s/scope.hpp"
#include "domain/k8s/store_budget.hpp"
#include "domain/k8s/store_error.hpp"
#include "domain/k8s/store_metrics.hpp"
#include "domain/k8s/watch_event.hpp"
#include "ports/log/logger.hpp"

namespace antares::application::k8s {

/// The one store: a scope per `(cluster, group-version-resource, namespace)`
/// and one entry per uid inside it (FR-001).
///
/// The store is confined to one thread (FR-009). Under ADR-0020 that is the
/// model's own loop, so there is no lock: a batch decoded by a parse worker
/// crosses as a value and is applied here.
///
/// Retention is bounded by admission and never by eviction (FR-004). Past a
/// ceiling an object is refused, counted and its scope published `truncated`,
/// so `rowCount` never shrinks under a scrollbar; an entry leaves only on a
/// `DELETED` or on the reconciliation sweep of a finished relist (FR-007).
class ObjectStore {
public:
    /// Receives one change set per applied batch, on the store's thread.
    using Listener = std::function<void(const domain::k8s::ChangeSet &)>;

    explicit ObjectStore(domain::k8s::StoreBudget budget = {},
                         ports::log::Logger *logger = nullptr);

    ObjectStore(const ObjectStore &) = delete;
    ObjectStore &operator=(const ObjectStore &) = delete;
    ObjectStore(ObjectStore &&) = delete;
    ObjectStore &operator=(ObjectStore &&) = delete;
    ~ObjectStore() = default;

    /// Open the scope, or hand back the one already open (FR-001). A second
    /// view of the same scope is free; past `maxScopes` the open is refused.
    [[nodiscard]] std::expected<StoreScope *, domain::k8s::StoreError>
    open(const domain::k8s::Scope &scope);

    [[nodiscard]] StoreScope *find(const domain::k8s::Scope &scope) noexcept;
    [[nodiscard]] const StoreScope *find(const domain::k8s::Scope &scope) const noexcept;

    /// Close a scope and forget its rows. The pool dies with the scope, so no
    /// intern can outlive it.
    void close(const domain::k8s::Scope &scope) noexcept;

    /// The stored entry for a key, or nothing. Two callers resolving the same
    /// key get the same pointer, which is the whole point of FR-001; it stays
    /// valid until the next batch.
    [[nodiscard]] const domain::k8s::Entry *resolve(const domain::k8s::ObjectKey &key) const;

    /// Apply one batch of deltas and return what moved, in row terms (FR-006,
    /// FR-008). A bookmark touches no entry. An unknown uid on a `MODIFIED` or
    /// `DELETED` is counted, not failed.
    [[nodiscard]] domain::k8s::ChangeSet apply(const domain::k8s::Scope &scope,
                                               std::span<const domain::k8s::WatchEvent> batch);

    /// Open a relist epoch (FR-007). Every page that follows is marked with it.
    [[nodiscard]] std::expected<std::uint64_t, domain::k8s::StoreError>
    beginRelist(const domain::k8s::Scope &scope);

    /// Upsert one relist page under `epoch` and return its own change set.
    [[nodiscard]] std::expected<domain::k8s::ChangeSet, domain::k8s::StoreError>
    relistPage(const domain::k8s::Scope &scope, std::uint64_t epoch,
               std::span<const domain::k8s::Entry> page);

    /// The final page: sweep every row the epoch did not mark, in one batch.
    [[nodiscard]] std::expected<domain::k8s::ChangeSet, domain::k8s::StoreError>
    endRelist(const domain::k8s::Scope &scope, std::uint64_t epoch);

    /// Give up on a relist. Nothing is swept, so the scope keeps what it had
    /// and stays stale.
    void abandonRelist(const domain::k8s::Scope &scope) noexcept;

    /// A 410 expired the watch: the rows stay visible until the relist ends.
    void markStale(const domain::k8s::Scope &scope) noexcept;

    void setListener(Listener listener) { listener_ = std::move(listener); }

    [[nodiscard]] const domain::k8s::StoreMetrics &metrics() const noexcept { return metrics_; }
    [[nodiscard]] std::uint64_t totalEntries() const noexcept { return totalEntries_; }
    [[nodiscard]] std::size_t openScopes() const noexcept { return scopes_.size(); }

private:
    [[nodiscard]] bool admits(const StoreScope &scope) const noexcept;
    void refuse(StoreScope &scope, const domain::k8s::Scope &key);
    void grew(const domain::k8s::Scope &scope, std::size_t count) noexcept;
    void shrank(const domain::k8s::Scope &scope, std::size_t count) noexcept;
    void publish(const domain::k8s::ChangeSet &changes);

    domain::k8s::StoreBudget budget_;
    ports::log::Logger *logger_ = nullptr;
    domain::k8s::StoreMetrics metrics_;
    std::unordered_map<domain::k8s::Scope, StoreScope, domain::k8s::ScopeHash> scopes_;
    Listener listener_;
    std::uint64_t totalEntries_ = 0;
    /// Live entries per `ScopeKind`, so `store.entries` is a gauge rather than
    /// a re-count.
    std::array<std::uint64_t, 2> entriesByKind_{};
    /// Refusals per `ScopeKind`, the running total `store.refused` publishes.
    std::array<std::uint64_t, 2> refusedByKind_{};
};

} // namespace antares::application::k8s
