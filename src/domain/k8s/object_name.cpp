#include "domain/k8s/object_name.hpp"

#include <expected>
#include <string_view>

namespace antares::domain::k8s {

std::expected<ObjectName, StoreError> ObjectName::of(std::string_view text)
{
    if (text.empty() || text.size() > ceiling) {
        return std::unexpected(StoreError::invalidName);
    }
    if (text == "." || text == "..") {
        return std::unexpected(StoreError::invalidName);
    }
    // `/` and `%` are the two characters that would change the path or the
    // query if a name were spliced into one unescaped.
    if (text.find_first_of("/%") != std::string_view::npos) {
        return std::unexpected(StoreError::invalidName);
    }

    ObjectName name;
    name.text_ = std::string{text};
    return name;
}

} // namespace antares::domain::k8s
