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
    }
    return {};
}

} // namespace antares::adapters::ui
