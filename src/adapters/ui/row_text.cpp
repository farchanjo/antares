#include "adapters/ui/row_text.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#include "domain/k8s/status_summary.hpp"

namespace antares::adapters::ui {

namespace {

/// The bounded status summary as one line: the reason when the server gave one,
/// otherwise the ready/total pair. Both numbers are optional because most kinds
/// carry neither.
[[nodiscard]] std::string statusText(const domain::k8s::StatusSummary &status)
{
    if (!status.reason.empty()) {
        return status.reason;
    }
    if (!status.ready || !status.total) {
        return {};
    }
    return std::to_string(*status.ready) + "/" + std::to_string(*status.total);
}

} // namespace

std::string rowText(const domain::k8s::Entry &entry, const domain::k8s::Scope &scope, RowRole role)
{
    switch (role) {
    case RowRole::name:
        return entry.identity.name;
    case RowRole::namespaceName:
        return scope.namespaceName.text();
    case RowRole::status:
        return statusText(entry.status);
    case RowRole::age:
        return entry.creationTimestamp;
    case RowRole::conditions:
        return entry.status.reason;
    case RowRole::cpu:
    case RowRole::memory:
    case RowRole::disk:
    case RowRole::restarts:
    case RowRole::node:
    case RowRole::qos:
    case RowRole::controlledBy:
    case RowRole::version:
    case RowRole::labels:
    case RowRole::keys:
    case RowRole::type:
    case RowRole::webhooks:
    case RowRole::taints:
    case RowRole::roles:
    case RowRole::clusterIp:
    case RowRole::ports:
    case RowRole::externalIp:
    case RowRole::selector:
    case RowRole::schedule:
    case RowRole::suspend:
    case RowRole::active:
    case RowRole::lastSchedule:
    case RowRole::nextExecution:
    case RowRole::timezone:
    case RowRole::metrics:
    case RowRole::minPods:
    case RowRole::maxPods:
    case RowRole::replicas:
    case RowRole::minAvailable:
    case RowRole::maxUnavailable:
    case RowRole::currentHealthy:
    case RowRole::desiredHealthy:
    case RowRole::value:
    case RowRole::globalDefault:
    case RowRole::holder:
    case RowRole::completions:
    case RowRole::argocd:
        // The projection does not carry these yet; an empty cell is honest and
        // the column keeps its place.
        return {};
    }
    return {};
}

} // namespace antares::adapters::ui
