#include "adapters/ui/row_role.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace antares::adapters::ui {

using domain::k8s::EntryField;

namespace {

/// Append a role once, in role order, so a caller replays a stable list.
void addRole(std::array<RowRole, ROW_ROLE_COUNT> &scratch, std::size_t &size, RowRole role) noexcept
{
    const std::span<const RowRole> written{scratch.data(), size};
    if (std::find(written.begin(), written.end(), role) != written.end()) {
        return;
    }
    std::size_t at = size;
    while (at > 0 && static_cast<std::uint8_t>(scratch[at - 1]) > static_cast<std::uint8_t>(role)) {
        scratch[at] = scratch[at - 1];
        --at;
    }
    scratch[at] = role;
    ++size;
}

} // namespace

std::string_view rowRoleName(RowRole role) noexcept
{
    switch (role) {
    case RowRole::name:
        return "name";
    case RowRole::namespaceName:
        return "namespace";
    case RowRole::status:
        return "status";
    case RowRole::age:
        return "age";
    }
    return "name";
}

std::optional<RowRole> roleForIndex(std::uint32_t index) noexcept
{
    switch (index) {
    case 257:
        return RowRole::name;
    case 258:
        return RowRole::namespaceName;
    case 259:
        return RowRole::status;
    case 260:
        return RowRole::age;
    default:
        return std::nullopt;
    }
}

bool carriesOrderingKey(std::span<const EntryField> fields) noexcept
{
    return std::find(fields.begin(), fields.end(), orderingKey()) != fields.end();
}

std::span<const RowRole> rolesFor(std::span<const EntryField> fields,
                                  std::array<RowRole, ROW_ROLE_COUNT> &scratch) noexcept
{
    std::size_t size = 0;
    for (const EntryField field : fields) {
        switch (field) {
        case EntryField::status:
        case EntryField::resourceVersion:
            addRole(scratch, size, RowRole::status);
            break;
        case EntryField::generation:
            // The revision is the row's second line, so a new generation
            // repaints both the title and the status text.
            addRole(scratch, size, RowRole::name);
            addRole(scratch, size, RowRole::status);
            break;
        case EntryField::labels:
            addRole(scratch, size, RowRole::name);
            break;
        }
    }
    return std::span<const RowRole>{scratch.data(), size};
}

} // namespace antares::adapters::ui
