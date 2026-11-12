#include "domain/k8s/namespace_name.hpp"

#include <expected>
#include <string_view>

#include "domain/k8s/api_path.hpp"

namespace antares::domain::k8s {

std::expected<NamespaceName, StoreError> NamespaceName::of(std::string_view text)
{
    if (text.empty()) {
        return NamespaceName{};
    }
    if (text.size() > ceiling || !isDnsLabel(text)) {
        return std::unexpected(StoreError::invalidNamespace);
    }

    NamespaceName name;
    name.text_ = std::string{text};
    return name;
}

} // namespace antares::domain::k8s
