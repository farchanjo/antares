#include "adapters/ui/family.hpp"

#include <array>

namespace antares::adapters::ui {

namespace {

// One table per family, exactly the columns the reference shows.
constexpr std::array<RowRole, 10> PODS{
    RowRole::name,      RowRole::namespaceName, RowRole::cpu,
    RowRole::memory,    RowRole::restarts,      RowRole::controlledBy,
    RowRole::node,      RowRole::qos,           RowRole::status,
    RowRole::age,
};
constexpr std::array<RowRole, 4> WORKLOAD{RowRole::name, RowRole::namespaceName, RowRole::status,
                                          RowRole::age};
constexpr std::array<RowRole, 7> JOBS{RowRole::name, RowRole::namespaceName, RowRole::completions,
                                      RowRole::age,  RowRole::conditions,    RowRole::argocd,
                                      RowRole::status};
constexpr std::array<RowRole, 11> CRON_JOBS{
    RowRole::name,        RowRole::namespaceName, RowRole::schedule,     RowRole::suspend,
    RowRole::active,      RowRole::lastSchedule,  RowRole::nextExecution, RowRole::argocd,
    RowRole::timezone,    RowRole::age,           RowRole::status,
};
constexpr std::array<RowRole, 9> NODES{RowRole::name,   RowRole::cpu,        RowRole::memory,
                                       RowRole::disk,   RowRole::taints,     RowRole::roles,
                                       RowRole::version, RowRole::age,       RowRole::conditions};
constexpr std::array<RowRole, 4> NAMESPACES{RowRole::name, RowRole::status, RowRole::age,
                                            RowRole::labels};
constexpr std::array<RowRole, 7> EVENTS{RowRole::name, RowRole::namespaceName, RowRole::type,
                                        RowRole::conditions, RowRole::node, RowRole::age,
                                        RowRole::status};
constexpr std::array<RowRole, 9> SERVICES{
    RowRole::name,     RowRole::namespaceName, RowRole::type,   RowRole::clusterIp,
    RowRole::ports,    RowRole::externalIp,    RowRole::selector, RowRole::age,
    RowRole::status,
};
constexpr std::array<RowRole, 4> ENDPOINTS{RowRole::name, RowRole::namespaceName, RowRole::ports,
                                           RowRole::age};
constexpr std::array<RowRole, 4> INGRESSES{RowRole::name, RowRole::namespaceName, RowRole::ports,
                                           RowRole::age};
constexpr std::array<RowRole, 5> POLICIES{RowRole::name, RowRole::namespaceName, RowRole::labels,
                                          RowRole::age, RowRole::status};
constexpr std::array<RowRole, 5> CONFIG_MAPS{RowRole::name, RowRole::namespaceName, RowRole::keys,
                                             RowRole::age, RowRole::labels};
constexpr std::array<RowRole, 6> SECRETS{RowRole::name, RowRole::namespaceName, RowRole::labels,
                                         RowRole::keys, RowRole::type, RowRole::age};
constexpr std::array<RowRole, 4> QUOTAS{RowRole::name, RowRole::namespaceName, RowRole::age,
                                        RowRole::status};
constexpr std::array<RowRole, 10> AUTOSCALERS{
    RowRole::name,        RowRole::namespaceName, RowRole::metrics,        RowRole::minPods,
    RowRole::maxPods,     RowRole::replicas,      RowRole::age,            RowRole::status,
    RowRole::conditions,  RowRole::labels,
};
constexpr std::array<RowRole, 8> BUDGETS{RowRole::name,           RowRole::namespaceName,
                                         RowRole::minAvailable,   RowRole::maxUnavailable,
                                         RowRole::currentHealthy, RowRole::desiredHealthy,
                                         RowRole::age,            RowRole::status};
constexpr std::array<RowRole, 5> PRIORITY{RowRole::name, RowRole::value, RowRole::globalDefault,
                                          RowRole::age, RowRole::labels};
constexpr std::array<RowRole, 4> RUNTIME{RowRole::name, RowRole::holder, RowRole::age,
                                         RowRole::labels};
constexpr std::array<RowRole, 5> LEASES{RowRole::name, RowRole::namespaceName, RowRole::holder,
                                        RowRole::age, RowRole::status};
constexpr std::array<RowRole, 6> CLAIMS{RowRole::name, RowRole::namespaceName, RowRole::status,
                                        RowRole::age,  RowRole::labels,        RowRole::conditions};
constexpr std::array<RowRole, 8> VOLUMES{RowRole::name,   RowRole::status, RowRole::age,
                                         RowRole::labels, RowRole::age,    RowRole::conditions,
                                         RowRole::type,   RowRole::node};
constexpr std::array<RowRole, 5> CLASSES{RowRole::name, RowRole::type, RowRole::globalDefault,
                                         RowRole::age, RowRole::labels};
constexpr std::array<RowRole, 3> ACCOUNTS{RowRole::name, RowRole::namespaceName, RowRole::age};
constexpr std::array<RowRole, 4> BINDINGS{RowRole::name, RowRole::namespaceName, RowRole::roles,
                                          RowRole::age};
constexpr std::array<RowRole, 3> CUSTOM{RowRole::name, RowRole::namespaceName, RowRole::age};

} // namespace

std::span<const RowRole> familyColumns(Family family) noexcept
{
    switch (family) {
    case Family::pods:
        return PODS;
    case Family::jobs:
        return JOBS;
    case Family::cronJobs:
        return CRON_JOBS;
    case Family::nodes:
        return NODES;
    case Family::namespaces:
        return NAMESPACES;
    case Family::events:
        return EVENTS;
    case Family::services:
        return SERVICES;
    case Family::endpointSlices:
    case Family::endpoints:
        return ENDPOINTS;
    case Family::ingresses:
        return INGRESSES;
    case Family::networkPolicies:
        return POLICIES;
    case Family::configMaps:
        return CONFIG_MAPS;
    case Family::secrets:
        return SECRETS;
    case Family::resourceQuotas:
    case Family::limitRanges:
        return QUOTAS;
    case Family::horizontalPodAutoscalers:
        return AUTOSCALERS;
    case Family::podDisruptionBudgets:
        return BUDGETS;
    case Family::priorityClasses:
        return PRIORITY;
    case Family::runtimeClasses:
        return RUNTIME;
    case Family::leases:
        return LEASES;
    case Family::persistentVolumeClaims:
        return CLAIMS;
    case Family::persistentVolumes:
        return VOLUMES;
    case Family::storageClasses:
        return CLASSES;
    case Family::serviceAccounts:
        return ACCOUNTS;
    case Family::roles:
    case Family::roleBindings:
    case Family::clusterRoles:
    case Family::clusterRoleBindings:
        return BINDINGS;
    case Family::webhookConfigurations:
        return BINDINGS;
    case Family::customResources:
        return CUSTOM;
    case Family::deployments:
    case Family::daemonSets:
    case Family::statefulSets:
    case Family::replicaSets:
    case Family::replicationControllers:
        return WORKLOAD;
    }
    return WORKLOAD;
}

} // namespace antares::adapters::ui
