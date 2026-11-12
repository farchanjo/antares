#include "application/k8s/object_store.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace antares::application::k8s {

namespace {

using domain::k8s::ChangedRow;
using domain::k8s::ChangeSet;
using domain::k8s::Entry;
using domain::k8s::EntryField;
using domain::k8s::EventType;
using domain::k8s::RowRange;
using domain::k8s::Scope;
using domain::k8s::StoreError;

/// Contiguous runs of an ascending, unique row list, emitted descending so a
/// model can remove from the bottom up.
[[nodiscard]] std::vector<RowRange> rangesDescending(const std::vector<std::size_t> &rows)
{
    std::vector<RowRange> ranges;
    for (auto cursor = rows.rbegin(); cursor != rows.rend();) {
        auto first = static_cast<std::uint32_t>(*cursor);
        auto last = static_cast<std::uint32_t>(*cursor + 1);
        ++cursor;
        while (cursor != rows.rend() && *cursor + 1 == first) {
            first = static_cast<std::uint32_t>(*cursor);
            ++cursor;
        }
        ranges.push_back(RowRange{first, last});
    }
    return ranges;
}

/// The same runs, ascending, against the rows that remain.
[[nodiscard]] std::vector<RowRange> rangesAscending(const std::vector<std::size_t> &rows)
{
    std::vector<RowRange> ranges;
    for (std::size_t index = 0; index < rows.size();) {
        auto first = static_cast<std::uint32_t>(rows[index]);
        auto last = static_cast<std::uint32_t>(rows[index] + 1);
        ++index;
        while (index < rows.size() && rows[index] == last) {
            ++last;
            ++index;
        }
        ranges.push_back(RowRange{first, last});
    }
    return ranges;
}

/// Fold one delta into the changed-row list. Several events for one uid yield
/// one change with the masks merged (FR-008), and an empty mask is dropped so
/// an unrendered field repaints nothing.
void recordChange(std::vector<ChangedRow> &changes, std::size_t row, const Entry &before,
                  const Entry &after)
{
    std::vector<EntryField> fields = ChangeSet::diff(before, after);
    if (fields.empty()) {
        return;
    }

    const auto existing = std::find_if(changes.begin(), changes.end(),
                                       [row](const ChangedRow &changed) {
                                           return changed.row == row;
                                       });
    if (existing == changes.end()) {
        changes.push_back(ChangedRow{static_cast<std::uint32_t>(row), std::move(fields)});
        return;
    }
    for (const EntryField field : fields) {
        if (std::find(existing->fields.begin(), existing->fields.end(), field) ==
            existing->fields.end()) {
            existing->fields.push_back(field);
        }
    }
}

} // namespace

ObjectStore::ObjectStore(domain::k8s::StoreBudget budget, ports::log::Logger *logger)
    : budget_(budget), logger_(logger)
{
}

std::expected<StoreScope *, StoreError> ObjectStore::open(const Scope &scope)
{
    if (const auto found = scopes_.find(scope); found != scopes_.end()) {
        return &found->second;
    }
    if (scope.kind.empty() || scope.kind.size() > 64) {
        return std::unexpected(StoreError::invalidKind);
    }
    if (scopes_.size() >= budget_.maxScopes) {
        if (logger_ != nullptr) {
            logger_->write(ports::log::Level::warn, "k8s",
                           "store refused a scope: " + scope.cluster.text() + " already has " +
                               std::to_string(scopes_.size()) + " open");
        }
        return std::unexpected(StoreError::tooManyScopes);
    }

    auto [position, inserted] = scopes_.try_emplace(scope, scope);
    static_cast<void>(inserted);
    return &position->second;
}

StoreScope *ObjectStore::find(const Scope &scope) noexcept
{
    const auto found = scopes_.find(scope);
    return found == scopes_.end() ? nullptr : &found->second;
}

const StoreScope *ObjectStore::find(const Scope &scope) const noexcept
{
    const auto found = scopes_.find(scope);
    return found == scopes_.end() ? nullptr : &found->second;
}

void ObjectStore::close(const Scope &scope) noexcept
{
    const auto found = scopes_.find(scope);
    if (found == scopes_.end()) {
        return;
    }
    shrank(scope, found->second.size());
    scopes_.erase(found);
}

const Entry *ObjectStore::resolve(const domain::k8s::ObjectKey &key) const
{
    const StoreScope *scope = find(key.scope);
    if (scope == nullptr) {
        return nullptr;
    }
    return scope->find(key.uid);
}

bool ObjectStore::admits(const StoreScope &scope) const noexcept
{
    return scope.size() < budget_.maxEntries && totalEntries_ < budget_.maxTotalEntries;
}

void ObjectStore::refuse(StoreScope &scope, const Scope &key)
{
    ++scope.state().refused;
    scope.state().truncated = true;

    const auto kind = static_cast<std::size_t>(key.scopeKind());
    ++refusedByKind_[kind];
    metrics_.observeRefused(key.scopeKind(), refusedByKind_[kind]);

    if (logger_ != nullptr) {
        logger_->write(ports::log::Level::warn, "k8s",
                       "store refused an object at its ceiling: " + key.cluster.text() + "/" +
                           key.type.resource + " refused " +
                           std::to_string(scope.state().refused));
    }
}

void ObjectStore::grew(const Scope &scope, std::size_t count) noexcept
{
    totalEntries_ += count;
    const auto kind = static_cast<std::size_t>(scope.scopeKind());
    entriesByKind_[kind] += count;
    metrics_.observeEntries(scope.scopeKind(), entriesByKind_[kind]);
}

void ObjectStore::shrank(const Scope &scope, std::size_t count) noexcept
{
    const auto kind = static_cast<std::size_t>(scope.scopeKind());
    totalEntries_ -= std::min<std::uint64_t>(totalEntries_, count);
    entriesByKind_[kind] -= std::min<std::uint64_t>(entriesByKind_[kind], count);
    metrics_.observeEntries(scope.scopeKind(), entriesByKind_[kind]);
}

void ObjectStore::publish(const ChangeSet &changes)
{
    if (listener_ && !changes.empty()) {
        listener_(changes);
    }
}

ChangeSet ObjectStore::apply(const Scope &scope, std::span<const domain::k8s::WatchEvent> batch)
{
    ChangeSet changes;
    StoreScope *target = find(scope);
    if (target == nullptr) {
        return changes;
    }

    // Removal indices are stated against the rows as they were before the
    // batch, so appends during the batch cannot renumber them; the compaction
    // at the end is the only renumbering (FR-008).
    const std::size_t before = target->rows().size();
    std::vector<std::size_t> removed;
    std::vector<std::size_t> inserted;
    std::vector<ChangedRow> changed;

    for (const domain::k8s::WatchEvent &event : batch) {
        if (!event.entry) {
            continue;
        }
        const Entry &entry = *event.entry;
        const std::optional<std::size_t> row = target->rowOf(entry.uid());

        switch (event.type) {
        case EventType::bookmark:
        case EventType::error:
            break;
        case EventType::added:
        case EventType::modified: {
            if (row) {
                recordChange(changed, *row, target->at(*row), entry);
                target->replace(*row, entry);
                break;
            }
            // An unknown uid on MODIFIED is what a refusal looks like from the
            // stream, so it is counted rather than failed (FR-006).
            if (event.type == EventType::modified) {
                ++target->state().unknown;
                break;
            }
            if (!admits(*target)) {
                refuse(*target, scope);
                break;
            }
            inserted.push_back(target->append(entry));
            grew(scope, 1);
            break;
        }
        case EventType::deleted: {
            if (!row) {
                ++target->state().unknown;
                break;
            }
            if (*row >= before) {
                // Inserted and removed inside one batch: neither range states
                // it, so a mirror never sees a row that never existed.
                std::erase(inserted, *row);
            } else {
                removed.push_back(*row);
            }
            target->erase(*row);
            shrank(scope, 1);
            break;
        }
        }
    }

    std::sort(removed.begin(), removed.end());
    removed.erase(std::unique(removed.begin(), removed.end()), removed.end());

    std::vector<std::size_t> shifted;
    shifted.reserve(inserted.size());
    for (const std::size_t row : inserted) {
        shifted.push_back(row - static_cast<std::size_t>(
                                    std::lower_bound(removed.begin(), removed.end(), row) -
                                    removed.begin()));
    }
    std::sort(shifted.begin(), shifted.end());
    shifted.erase(std::unique(shifted.begin(), shifted.end()), shifted.end());

    changes.removed = rangesDescending(removed);
    changes.inserted = rangesAscending(shifted);

    std::sort(changed.begin(), changed.end(), [](const ChangedRow &left, const ChangedRow &right) {
        return left.row < right.row;
    });
    for (ChangedRow &entry : changed) {
        const std::size_t row = entry.row;
        if (std::binary_search(removed.begin(), removed.end(), row)) {
            continue;
        }
        entry.row = static_cast<std::uint32_t>(
            row - static_cast<std::size_t>(std::lower_bound(removed.begin(), removed.end(), row) -
                                           removed.begin()));
        changes.changed.push_back(std::move(entry));
    }

    if (!removed.empty()) {
        target->compact();
    }
    publish(changes);
    return changes;
}

std::expected<std::uint64_t, StoreError> ObjectStore::beginRelist(const Scope &scope)
{
    StoreScope *target = find(scope);
    if (target == nullptr) {
        return std::unexpected(StoreError::unknownScope);
    }
    return target->beginEpoch();
}

std::expected<ChangeSet, StoreError> ObjectStore::relistPage(const Scope &scope, std::uint64_t epoch,
                                                            std::span<const Entry> page)
{
    StoreScope *target = find(scope);
    if (target == nullptr) {
        return std::unexpected(StoreError::unknownScope);
    }
    if (!target->epochIs(epoch)) {
        return std::unexpected(StoreError::notRelisting);
    }

    // A page is a batch of its own (FR-008); admission is the same rule a
    // delta meets, so a relist past the ceiling truncates instead of growing
    // past it (FR-004).
    ChangeSet changes;
    std::vector<std::size_t> inserted;
    std::vector<ChangedRow> changed;

    for (const Entry &entry : page) {
        target->markSeen(entry.uid());
        const std::optional<std::size_t> row = target->rowOf(entry.uid());
        if (row) {
            recordChange(changed, *row, target->at(*row), entry);
            target->replace(*row, entry);
            continue;
        }
        if (!admits(*target)) {
            refuse(*target, scope);
            continue;
        }
        inserted.push_back(target->append(entry));
        grew(scope, 1);
    }

    std::sort(inserted.begin(), inserted.end());
    changes.inserted = rangesAscending(inserted);
    changes.changed = std::move(changed);

    publish(changes);
    return changes;
}

std::expected<ChangeSet, StoreError> ObjectStore::endRelist(const Scope &scope,
                                                            std::uint64_t epoch)
{
    StoreScope *target = find(scope);
    if (target == nullptr) {
        return std::unexpected(StoreError::unknownScope);
    }
    if (!target->epochIs(epoch)) {
        return std::unexpected(StoreError::notRelisting);
    }

    // One sweep, stated against the rows as they stand, so the count after the
    // relist is the relist and never the sum of both (FR-007).
    std::vector<std::size_t> removed;
    const auto rows = target->rows();
    for (std::size_t row = 0; row < rows.size(); ++row) {
        const std::optional<Entry> &slot = rows[row];
        if (slot && !target->seen(slot->uid())) {
            removed.push_back(row);
        }
    }
    std::sort(removed.begin(), removed.end());
    for (const std::size_t row : removed) {
        target->erase(row);
    }
    shrank(scope, removed.size());
    if (!removed.empty()) {
        target->compact();
    }
    target->endEpoch();
    target->state().stale = false;

    ChangeSet changes;
    changes.removed = rangesDescending(removed);
    publish(changes);
    return changes;
}

void ObjectStore::abandonRelist(const Scope &scope) noexcept
{
    if (StoreScope *target = find(scope); target != nullptr) {
        // Nothing is swept: the rows the epoch never reached are still the
        // server's rows, and the scope stays stale until a relist finishes.
        target->endEpoch();
        target->state().stale = true;
    }
}

void ObjectStore::markStale(const Scope &scope) noexcept
{
    if (StoreScope *target = find(scope); target != nullptr) {
        target->markStale();
        if (logger_ != nullptr) {
            logger_->write(ports::log::Level::warn, "k8s",
                           "watch expired, scope is stale: " + scope.cluster.text() + "/" +
                               scope.type.resource);
        }
    }
}

} // namespace antares::application::k8s
