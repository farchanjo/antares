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
enum class RowRole : std::uint8_t {
    name,
    namespaceName,
    status,
    age,
    // The rest exist because the reference gives every family its own columns.
    // A family names the roles it paints; nothing paints all of them.
    labels,
    keys,
    type,
    cpu,
    memory,
    disk,
    restarts,
    controlledBy,
    node,
    qos,
    conditions,
    webhooks,
    version,
    taints,
    roles,
    clusterIp,
    ports,
    externalIp,
    selector,
    schedule,
    suspend,
    active,
    lastSchedule,
    nextExecution,
    timezone,
    metrics,
    minPods,
    maxPods,
    replicas,
    minAvailable,
    maxUnavailable,
    currentHealthy,
    desiredHealthy,
    value,
    globalDefault,
    holder,
    completions,
    argocd,
};

/// Roles one row can paint. Fixed so `rolesFor` never allocates.
inline constexpr std::size_t ROW_ROLE_COUNT = 42;

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
    case RowRole::labels:
        return 261;
    case RowRole::keys:
        return 262;
    case RowRole::type:
        return 263;
    case RowRole::cpu:
        return 264;
    case RowRole::memory:
        return 265;
    case RowRole::disk:
        return 266;
    case RowRole::restarts:
        return 267;
    case RowRole::controlledBy:
        return 268;
    case RowRole::node:
        return 269;
    case RowRole::qos:
        return 270;
    case RowRole::conditions:
        return 271;
    case RowRole::webhooks:
        return 272;
    case RowRole::version:
        return 273;
    case RowRole::taints:
        return 274;
    case RowRole::roles:
        return 275;
    case RowRole::clusterIp:
        return 276;
    case RowRole::ports:
        return 277;
    case RowRole::externalIp:
        return 278;
    case RowRole::selector:
        return 279;
    case RowRole::schedule:
        return 280;
    case RowRole::suspend:
        return 281;
    case RowRole::active:
        return 282;
    case RowRole::lastSchedule:
        return 283;
    case RowRole::nextExecution:
        return 284;
    case RowRole::timezone:
        return 285;
    case RowRole::metrics:
        return 286;
    case RowRole::minPods:
        return 287;
    case RowRole::maxPods:
        return 288;
    case RowRole::replicas:
        return 289;
    case RowRole::minAvailable:
        return 290;
    case RowRole::maxUnavailable:
        return 291;
    case RowRole::currentHealthy:
        return 292;
    case RowRole::desiredHealthy:
        return 293;
    case RowRole::value:
        return 294;
    case RowRole::globalDefault:
        return 295;
    case RowRole::holder:
        return 296;
    case RowRole::completions:
        return 297;
    case RowRole::argocd:
        return 298;
    }
    return 257;
}

/// The registered name of a role, for the delegate and the log line.
[[nodiscard]] std::string_view rowRoleName(RowRole role) noexcept;

/// The column heading a reader sees, in the reference's casing: `CPU`, `Memory`,
/// `Controlled By`. Separate from `rowRoleName` because the log wants the stable
/// identifier and the header wants prose.
[[nodiscard]] std::string_view rowRoleLabel(RowRole role) noexcept;

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
