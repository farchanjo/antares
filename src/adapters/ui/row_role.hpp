#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "domain/k8s/change_set.hpp"

namespace antares::adapters::ui {

/// The four columns a row renders, in display order.
///
/// Closed on purpose: the ids come from `render-the-resource-list.cue`
/// `#RoleIndex` and a delegate asks by name, so a role that is added is a
/// compile error here and not a silently blank column.
enum class RowRole : std::uint8_t { name, namespaceName, status, age };

/// Roles one row paints. Fixed so `rolesFor` never allocates.
inline constexpr std::size_t ROW_ROLE_COUNT = 4;

/// The integer a role is registered under, exactly `#RoleIndex`.
[[nodiscard]] constexpr std::uint32_t roleIndex(RowRole role) noexcept
{
    switch (role) {
    case RowRole::name:
        return 257;
    case RowRole::namespaceName:
        return 258;
    case RowRole::status:
        return 259;
    case RowRole::age:
        return 260;
    }
    return 257;
}

/// The registered name of a role, for the delegate and the log line.
[[nodiscard]] std::string_view rowRoleName(RowRole role) noexcept;

/// The role an integer id names, or nothing for an id the model never
/// registered.
[[nodiscard]] std::optional<RowRole> roleForIndex(std::uint32_t index) noexcept;

/// The entry field the list is ordered by (FR-005).
///
/// The store keeps rows in arrival order and replaces in place, so a position
/// only ever moves when the row's *revision* moves: `generation` is bumped by a
/// spec write and by nothing else, so a status heartbeat repaints a row while a
/// real change re-sorts it. `resourceVersion` is deliberately not the key — it
/// changes on every write, status included, and would turn every heartbeat into
/// a move.
[[nodiscard]] constexpr domain::k8s::EntryField orderingKey() noexcept
{
    return domain::k8s::EntryField::generation;
}

/// Whether a changed-row mask carries the ordering key.
[[nodiscard]] bool carriesOrderingKey(std::span<const domain::k8s::EntryField> fields) noexcept;

/// The roles a changed-row mask repaints, deduplicated and in role order, written
/// into `scratch`.
///
/// A mask never names `name` or `namespace` (both are scope and identity, which
/// no delta rewrites) nor `age` (it is stamped at creation), so those two roles
/// only ever appear as the neighbours of a field that did change: a new
/// generation rewrites the row's revision line and its status line.
[[nodiscard]] std::span<const RowRole> rolesFor(std::span<const domain::k8s::EntryField> fields,
                                               std::array<RowRole, ROW_ROLE_COUNT> &scratch) noexcept;

} // namespace antares::adapters::ui
