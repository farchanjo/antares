#pragma once

#include <span>

#include "adapters/ui/row_role.hpp"

namespace antares::adapters::ui {

/// A resource family, as the navigator lists it.
///
/// The reference gives every family its own columns; the family is what decides
/// which of the roles a view paints, in which order. The enum is the navigator
/// tree, flattened.
enum class Family : std::uint8_t {
    pods,
    deployments,
    daemonSets,
    statefulSets,
    replicaSets,
    replicationControllers,
    jobs,
    cronJobs,
    nodes,
    namespaces,
    events,
    services,
    endpointSlices,
    endpoints,
    ingresses,
    networkPolicies,
    configMaps,
    secrets,
    resourceQuotas,
    limitRanges,
    horizontalPodAutoscalers,
    podDisruptionBudgets,
    priorityClasses,
    runtimeClasses,
    leases,
    persistentVolumeClaims,
    persistentVolumes,
    storageClasses,
    serviceAccounts,
    roles,
    roleBindings,
    clusterRoles,
    clusterRoleBindings,
    webhookConfigurations,
    customResources,
};

/// The columns of a family, in display order.
///
/// Always starts with the selection checkbox and `Name`, and the action column
/// is added by the view, so this is the data columns only.
[[nodiscard]] std::span<const RowRole> familyColumns(Family family) noexcept;

/// The family's translatable label, as the navigator shows it.
[[nodiscard]] const char *familyLabel(Family family) noexcept;

/// The family's plural resource name, for the API path.
[[nodiscard]] const char *familyResource(Family family) noexcept;

/// The API group. Empty for a core resource.
[[nodiscard]] const char *familyGroup(Family family) noexcept;

/// The API version.
[[nodiscard]] const char *familyVersion(Family family) noexcept;

/// Whether the family is namespaced. A cluster-scoped family hides the
/// namespace column and the selector.
[[nodiscard]] bool familyIsNamespaced(Family family) noexcept;

} // namespace antares::adapters::ui
