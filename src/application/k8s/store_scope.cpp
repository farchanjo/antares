#include "application/k8s/store_scope.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace antares::application::k8s {

using domain::k8s::Entry;
using domain::k8s::Uid;

StoreScope::StoreScope(domain::k8s::Scope scope) : scope_(std::move(scope)) {}

const Entry &StoreScope::at(std::size_t row) const
{
    static const Entry absent;

    const std::optional<Entry> &slot = rows_[row];
    if (!slot) {
        return absent;
    }
    return *slot;
}

const Entry *StoreScope::find(const Uid &uid) const noexcept
{
    const auto found = index_.find(uid);
    if (found == index_.end()) {
        return nullptr;
    }
    const std::optional<Entry> &row = rows_[found->second];
    return row ? &*row : nullptr;
}

std::optional<std::size_t> StoreScope::rowOf(const Uid &uid) const noexcept
{
    const auto found = index_.find(uid);
    if (found == index_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::size_t StoreScope::append(Entry entry)
{
    const std::size_t row = rows_.size();
    index_.emplace(entry.uid(), row);
    rows_.emplace_back(std::move(entry));
    ++live_;
    return row;
}

void StoreScope::replace(std::size_t row, Entry entry)
{
    rows_[row] = std::optional<Entry>{std::move(entry)};
}

void StoreScope::erase(std::size_t row)
{
    const std::optional<Entry> &slot = rows_[row];
    if (!slot) {
        return;
    }
    index_.erase(slot->uid());
    rows_[row].reset();
    --live_;
}

void StoreScope::moveToTail(std::size_t row)
{
    if (row + 1 >= rows_.size() || !rows_[row]) {
        return;
    }
    // Push the slot first, then erase it under the same index: the vector may
    // reallocate, so only indices are carried across the two statements.
    rows_.push_back(std::move(rows_[row]));
    rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(row));

    index_.clear();
    index_.reserve(rows_.size());
    for (std::size_t at = 0; at < rows_.size(); ++at) {
        const std::optional<Entry> &slot = rows_[at];
        if (!slot) {
            continue;
        }
        index_.emplace(slot->uid(), at);
    }
}

void StoreScope::compact(){
    std::erase_if(rows_, [](const std::optional<Entry> &row) { return !row.has_value(); });
    index_.clear();
    index_.reserve(rows_.size());
    for (std::size_t row = 0; row < rows_.size(); ++row) {
        const std::optional<Entry> &slot = rows_[row];
        if (!slot) {
            continue;
        }
        index_.emplace(slot->uid(), row);
    }
}

std::uint64_t StoreScope::beginEpoch() noexcept
{
    seen_.clear();
    relisting_ = true;
    ++epoch_;
    return epoch_;
}

void StoreScope::markSeen(const Uid &uid)
{
    seen_.insert(uid);
}

bool StoreScope::seen(const Uid &uid) const
{
    return seen_.contains(uid);
}

void StoreScope::endEpoch() noexcept
{
    seen_.clear();
    relisting_ = false;
}

} // namespace antares::application::k8s
