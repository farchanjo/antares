#include "domain/k8s/change_set.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace antares::domain::k8s {

std::string_view entryFieldName(EntryField field) noexcept
{
    switch (field) {
    case EntryField::resourceVersion:
        return "resourceVersion";
    case EntryField::generation:
        return "generation";
    case EntryField::labels:
        return "labels";
    case EntryField::status:
        return "status";
    }
    return "resourceVersion";
}

std::vector<EntryField> ChangeSet::diff(const Entry &before, const Entry &after)
{
    std::vector<EntryField> fields;
    if (before.resourceVersion != after.resourceVersion) {
        fields.push_back(EntryField::resourceVersion);
    }
    if (before.generation != after.generation) {
        fields.push_back(EntryField::generation);
    }
    if (before.labels.blob() != after.labels.blob() ||
        before.labels.truncated() != after.labels.truncated()) {
        fields.push_back(EntryField::labels);
    }
    if (!(before.status == after.status)) {
        fields.push_back(EntryField::status);
    }
    return fields;
}

} // namespace antares::domain::k8s
