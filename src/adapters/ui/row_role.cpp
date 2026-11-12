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
    case RowRole::labels:
        return "labels";
    case RowRole::keys:
        return "keys";
    case RowRole::type:
        return "type";
    case RowRole::cpu:
        return "cpu";
    case RowRole::memory:
        return "memory";
    case RowRole::disk:
        return "disk";
    case RowRole::restarts:
        return "restarts";
    case RowRole::controlledBy:
        return "controlled-by";
    case RowRole::node:
        return "node";
    case RowRole::qos:
        return "qos";
    case RowRole::conditions:
        return "conditions";
    case RowRole::webhooks:
        return "webhooks";
    case RowRole::version:
        return "version";
    case RowRole::taints:
        return "taints";
    case RowRole::roles:
        return "roles";
    case RowRole::clusterIp:
        return "cluster-ip";
    case RowRole::ports:
        return "ports";
    case RowRole::externalIp:
        return "external-ip";
    case RowRole::selector:
        return "selector";
    case RowRole::schedule:
        return "schedule";
    case RowRole::suspend:
        return "suspend";
    case RowRole::active:
        return "active";
    case RowRole::lastSchedule:
        return "last-schedule";
    case RowRole::nextExecution:
        return "next-execution";
    case RowRole::timezone:
        return "timezone";
    case RowRole::metrics:
        return "metrics";
    case RowRole::minPods:
        return "min-pods";
    case RowRole::maxPods:
        return "max-pods";
    case RowRole::replicas:
        return "replicas";
    case RowRole::minAvailable:
        return "min-available";
    case RowRole::maxUnavailable:
        return "max-unavailable";
    case RowRole::currentHealthy:
        return "current-healthy";
    case RowRole::desiredHealthy:
        return "desired-healthy";
    case RowRole::value:
        return "value";
    case RowRole::globalDefault:
        return "global-default";
    case RowRole::holder:
        return "holder";
    case RowRole::completions:
        return "completions";
    case RowRole::argocd:
        return "argocd";
    }
    return "name";
}

std::string_view rowRoleLabel(RowRole role) noexcept
{
    switch (role) {
    case RowRole::name:
        return "Name";
    case RowRole::namespaceName:
        return "Namespace";
    case RowRole::status:
        return "Status";
    case RowRole::age:
        return "Age";
    case RowRole::labels:
        return "Labels";
    case RowRole::keys:
        return "Keys";
    case RowRole::type:
        return "Type";
    case RowRole::cpu:
        return "CPU";
    case RowRole::memory:
        return "Memory";
    case RowRole::disk:
        return "Disk";
    case RowRole::restarts:
        return "Restarts";
    case RowRole::controlledBy:
        return "Controlled By";
    case RowRole::node:
        return "Node";
    case RowRole::qos:
        return "QoS";
    case RowRole::conditions:
        return "Conditions";
    case RowRole::webhooks:
        return "Webhooks";
    case RowRole::version:
        return "Version";
    case RowRole::taints:
        return "Taints";
    case RowRole::roles:
        return "Roles";
    case RowRole::clusterIp:
        return "Cluster IP";
    case RowRole::ports:
        return "Ports";
    case RowRole::externalIp:
        return "External IP";
    case RowRole::selector:
        return "Selector";
    case RowRole::schedule:
        return "Schedule";
    case RowRole::suspend:
        return "Suspend";
    case RowRole::active:
        return "Active";
    case RowRole::lastSchedule:
        return "Last Schedule";
    case RowRole::nextExecution:
        return "Next Execution";
    case RowRole::timezone:
        return "Time Zone";
    case RowRole::metrics:
        return "Metrics";
    case RowRole::minPods:
        return "Min Pods";
    case RowRole::maxPods:
        return "Max Pods";
    case RowRole::replicas:
        return "Replicas";
    case RowRole::minAvailable:
        return "Min Available";
    case RowRole::maxUnavailable:
        return "Max Unavailable";
    case RowRole::currentHealthy:
        return "Current Healthy";
    case RowRole::desiredHealthy:
        return "Desired Healthy";
    case RowRole::value:
        return "Value";
    case RowRole::globalDefault:
        return "Global Default";
    case RowRole::holder:
        return "Holder";
    case RowRole::completions:
        return "Completions";
    case RowRole::argocd:
        return "ArgoCD";
    }
    return "Name";
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
    case 261:
        return RowRole::labels;
    case 262:
        return RowRole::keys;
    case 263:
        return RowRole::type;
    case 264:
        return RowRole::cpu;
    case 265:
        return RowRole::memory;
    case 266:
        return RowRole::disk;
    case 267:
        return RowRole::restarts;
    case 268:
        return RowRole::controlledBy;
    case 269:
        return RowRole::node;
    case 270:
        return RowRole::qos;
    case 271:
        return RowRole::conditions;
    case 272:
        return RowRole::webhooks;
    case 273:
        return RowRole::version;
    case 274:
        return RowRole::taints;
    case 275:
        return RowRole::roles;
    case 276:
        return RowRole::clusterIp;
    case 277:
        return RowRole::ports;
    case 278:
        return RowRole::externalIp;
    case 279:
        return RowRole::selector;
    case 280:
        return RowRole::schedule;
    case 281:
        return RowRole::suspend;
    case 282:
        return RowRole::active;
    case 283:
        return RowRole::lastSchedule;
    case 284:
        return RowRole::nextExecution;
    case 285:
        return RowRole::timezone;
    case 286:
        return RowRole::metrics;
    case 287:
        return RowRole::minPods;
    case 288:
        return RowRole::maxPods;
    case 289:
        return RowRole::replicas;
    case 290:
        return RowRole::minAvailable;
    case 291:
        return RowRole::maxUnavailable;
    case 292:
        return RowRole::currentHealthy;
    case 293:
        return RowRole::desiredHealthy;
    case 294:
        return RowRole::value;
    case 295:
        return RowRole::globalDefault;
    case 296:
        return RowRole::holder;
    case 297:
        return RowRole::completions;
    case 298:
        return RowRole::argocd;
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
