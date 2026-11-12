#include "application/k8s/object_store.hpp"

#include <utility>

namespace antares::application::k8s {

void ObjectStore::replace(std::vector<Entry> entries)
{
    rows_ = std::move(entries);
    ++revision_;
}

void ObjectStore::append(Entry entry)
{
    rows_.push_back(std::move(entry));
    ++revision_;
}

void ObjectStore::clear()
{
    rows_.clear();
    ++revision_;
}

std::size_t ObjectStore::size() const noexcept { return rows_.size(); }

bool ObjectStore::empty() const noexcept { return rows_.empty(); }

const Entry &ObjectStore::at(std::size_t index) const { return rows_.at(index); }

std::uint64_t ObjectStore::revision() const noexcept { return revision_; }

} // namespace antares::application::k8s
