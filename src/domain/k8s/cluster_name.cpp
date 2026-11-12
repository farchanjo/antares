#include "domain/k8s/cluster_name.hpp"

#include <expected>
#include <string_view>

namespace antares::domain::k8s {

std::expected<ClusterName, StoreError> ClusterName::of(std::string_view text)
{
    if (text.empty() || text.size() > ceiling) {
        return std::unexpected(StoreError::invalidCluster);
    }
    // A control character would break the one-line log entry a refusal writes.
    for (const char character : text) {
        const auto value = static_cast<unsigned char>(character);
        if (value < 0x20U || value == 0x7FU) {
            return std::unexpected(StoreError::invalidCluster);
        }
    }

    ClusterName name;
    name.text_ = std::string{text};
    return name;
}

} // namespace antares::domain::k8s
