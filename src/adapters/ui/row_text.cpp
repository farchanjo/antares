#include "adapters/ui/row_text.hpp"

#include <cstddef>
#include <cstdint>
#include <ctime>
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

/// The label blob as one comma-separated line, with the record separator turned
/// into `", "`. Truncated to the column's worth: a cell is a glance, not a
/// document.
[[nodiscard]] std::string labelsText(const domain::k8s::Labels &labels)
{
    constexpr std::size_t CEILING = 60;
    std::string_view blob = labels.blob();
    if (blob.size() > CEILING) {
        blob = blob.substr(0, CEILING);
    }
    std::string text{blob};
    for (std::size_t at = 0; at < text.size(); ++at) {
        if (text[at] == '\n') {
            text.replace(at, 1, ", ");
            ++at;
        }
    }
    return text;
}

/// The compact age the reference shows: `3y198d`, `53d`, `5d13h`. Read off the
/// RFC 3339 creation stamp by calendar arithmetic, so no clock is needed beyond
/// the current instant.
[[nodiscard]] std::string ageText(const std::string &stamp)
{
    if (stamp.size() < 20) {
        return {};
    }
    const auto number = [&stamp](std::size_t at, std::size_t len) {
        std::uint64_t value = 0;
        for (std::size_t digit = at; digit < at + len; ++digit) {
            if (stamp[digit] < '0' || stamp[digit] > '9') {
                return static_cast<std::uint64_t>(0);
            }
            value = value * 10 + static_cast<std::uint64_t>(stamp[digit] - '0');
        }
        return value;
    };
    const std::time_t then = [&stamp, &number] {
        std::tm parts{};
        parts.tm_year = static_cast<int>(number(0, 4)) - 1900;
        parts.tm_mon = static_cast<int>(number(5, 2)) - 1;
        parts.tm_mday = static_cast<int>(number(8, 2));
        parts.tm_hour = static_cast<int>(number(11, 2));
        parts.tm_min = static_cast<int>(number(14, 2));
        parts.tm_sec = static_cast<int>(number(17, 2));
        return ::timegm(&parts);
    }();
    const std::time_t now = std::time(nullptr);
    if (then <= 0 || now <= then) {
        return {};
    }
    const auto seconds = static_cast<std::uint64_t>(now - then);
    const std::uint64_t days = seconds / 86400;
    if (days < 1) {
        return std::to_string(seconds / 3600) + "h";
    }
    if (days < 365) {
        if (days < 30) {
            return std::to_string(days) + "d";
        }
        return std::to_string(days / 30) + "mo" + std::to_string(days % 30) + "d";
    }
    return std::to_string(days / 365) + "y" + std::to_string(days % 365) + "d";
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
        return ageText(entry.creationTimestamp);
    case RowRole::labels:
        return labelsText(entry.labels);
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
