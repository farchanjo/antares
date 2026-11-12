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


const char *familyLabel(Family family) noexcept
{
    switch (family) {
    case Family::pods:
        return "Pods";
    case Family::deployments:
        return "Deployments";
    case Family::daemonSets:
        return "Daemon Sets";
    case Family::statefulSets:
        return "Stateful Sets";
    case Family::replicaSets:
        return "Replica Sets";
    case Family::replicationControllers:
        return "Replication Controllers";
    case Family::jobs:
        return "Jobs";
    case Family::cronJobs:
        return "Cron Jobs";
    case Family::nodes:
        return "Nodes";
    case Family::namespaces:
        return "Namespaces";
    case Family::events:
        return "Events";
    case Family::services:
        return "Services";
    case Family::endpointSlices:
        return "Endpoint Slices";
    case Family::endpoints:
        return "Endpoints";
    case Family::ingresses:
        return "Ingresses";
    case Family::networkPolicies:
        return "Network Policies";
    case Family::configMaps:
        return "Config Maps";
    case Family::secrets:
        return "Secrets";
    case Family::resourceQuotas:
        return "Resource Quotas";
    case Family::limitRanges:
        return "Limit Ranges";
    case Family::horizontalPodAutoscalers:
        return "Horizontal Pod Autoscalers";
    case Family::podDisruptionBudgets:
        return "Pod Disruption Budgets";
    case Family::priorityClasses:
        return "Priority Classes";
    case Family::runtimeClasses:
        return "Runtime Classes";
    case Family::leases:
        return "Leases";
    case Family::persistentVolumeClaims:
        return "Persistent Volume Claims";
    case Family::persistentVolumes:
        return "Persistent Volumes";
    case Family::storageClasses:
        return "Storage Classes";
    case Family::serviceAccounts:
        return "Service Accounts";
    case Family::roles:
        return "Roles";
    case Family::roleBindings:
        return "Role Bindings";
    case Family::clusterRoles:
        return "Cluster Roles";
    case Family::clusterRoleBindings:
        return "Cluster Role Bindings";
    case Family::webhookConfigurations:
        return "Webhook Configurations";
    case Family::customResources:
        return "Custom Resources";
    }
    return "Pods";
}

const char *familyResource(Family family) noexcept
{
    switch (family) {
    case Family::pods:
        return "pods";
    case Family::deployments:
        return "deployments";
    case Family::daemonSets:
        return "daemonsets";
    case Family::statefulSets:
        return "statefulsets";
    case Family::replicaSets:
        return "replicasets";
    case Family::replicationControllers:
        return "replicationcontrollers";
    case Family::jobs:
        return "jobs";
    case Family::cronJobs:
        return "cronjobs";
    case Family::nodes:
        return "nodes";
    case Family::namespaces:
        return "namespaces";
    case Family::events:
        return "events";
    case Family::services:
        return "services";
    case Family::endpointSlices:
        return "endpointslices";
    case Family::endpoints:
        return "endpoints";
    case Family::ingresses:
        return "ingresses";
    case Family::networkPolicies:
        return "networkpolicies";
    case Family::configMaps:
        return "configmaps";
    case Family::secrets:
        return "secrets";
    case Family::resourceQuotas:
        return "resourcequotas";
    case Family::limitRanges:
        return "limitranges";
    case Family::horizontalPodAutoscalers:
        return "horizontalpodautoscalers";
    case Family::podDisruptionBudgets:
        return "poddisruptionbudgets";
    case Family::priorityClasses:
        return "priorityclasses";
    case Family::runtimeClasses:
        return "runtimeclasses";
    case Family::leases:
        return "leases";
    case Family::persistentVolumeClaims:
        return "persistentvolumeclaims";
    case Family::persistentVolumes:
        return "persistentvolumes";
    case Family::storageClasses:
        return "storageclasses";
    case Family::serviceAccounts:
        return "serviceaccounts";
    case Family::roles:
        return "roles";
    case Family::roleBindings:
        return "rolebindings";
    case Family::clusterRoles:
        return "clusterroles";
    case Family::clusterRoleBindings:
        return "clusterrolebindings";
    case Family::webhookConfigurations:
        return "validatingwebhookconfigurations";
    case Family::customResources:
        return "customresourcedefinitions";
    }
    return "pods";
}

const char *familyGroup(Family family) noexcept
{
    // Everything antares lists today is core or a well-known group; a custom
    // resource will carry its own and this becomes a lookup.
    switch (family) {
    case Family::deployments:
    case Family::daemonSets:
    case Family::statefulSets:
    case Family::replicaSets:
        return "apps";
    case Family::jobs:
    case Family::cronJobs:
        return "batch";
    case Family::ingresses:
    case Family::networkPolicies:
        return "networking.k8s.io";
    case Family::horizontalPodAutoscalers:
        return "autoscaling";
    case Family::podDisruptionBudgets:
        return "policy";
    case Family::priorityClasses:
    case Family::runtimeClasses:
    case Family::leases:
        return "scheduling.k8s.io";
    case Family::storageClasses:
        return "storage.k8s.io";
    case Family::roles:
    case Family::roleBindings:
    case Family::clusterRoles:
    case Family::clusterRoleBindings:
        return "rbac.authorization.k8s.io";
    case Family::webhookConfigurations:
        return "admissionregistration.k8s.io";
    case Family::customResources:
        return "apiextensions.k8s.io";
    case Family::endpointSlices:
        return "discovery.k8s.io";
    default:
        return "";
    }
}

const char *familyVersion(Family family) noexcept
{
    switch (family) {
    case Family::horizontalPodAutoscalers:
        return "v2";
    default:
        return "v1";
    }
}

bool familyIsNamespaced(Family family) noexcept
{
    switch (family) {
    case Family::nodes:
    case Family::namespaces:
    case Family::priorityClasses:
    case Family::runtimeClasses:
    case Family::persistentVolumes:
    case Family::storageClasses:
    case Family::clusterRoles:
    case Family::clusterRoleBindings:
    case Family::webhookConfigurations:
    case Family::customResources:
        return false;
    default:
        return true;
    }
}

} // namespace antares::adapters::ui

