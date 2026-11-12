#include "domain/k8s/api_path.hpp"

#include <algorithm>
#include <expected>
#include <utility>

namespace antares::domain::k8s {

namespace {

constexpr std::size_t LABEL_CEILING = 63;
constexpr std::size_t SUBDOMAIN_CEILING = 253;

[[nodiscard]] bool isLowerAlnum(char value) noexcept
{
    return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9');
}

[[nodiscard]] ApiError rejected(std::string message)
{
    return ApiError{Reason::invalid, 422, std::move(message)};
}

} // namespace

std::string_view subresourceName(Subresource subresource) noexcept
{
    switch (subresource) {
    case Subresource::none:
        return "";
    case Subresource::status:
        return "status";
    case Subresource::scale:
        return "scale";
    case Subresource::log:
        return "log";
    case Subresource::exec:
        return "exec";
    }
    return "";
}

bool isDnsLabel(std::string_view text) noexcept
{
    if (text.empty() || text.size() > LABEL_CEILING) {
        return false;
    }
    if (!isLowerAlnum(text.front()) || !isLowerAlnum(text.back())) {
        return false;
    }
    return std::ranges::all_of(text, [](char value) {
        return isLowerAlnum(value) || value == '-';
    });
}

bool isPathSegment(std::string_view text) noexcept
{
    if (text.empty() || text.size() > SEGMENT_CEILING) {
        return false;
    }
    if (text == "." || text == "..") {
        return false;
    }
    return text.find_first_of("/%") == std::string_view::npos;
}

bool isDnsSubdomain(std::string_view text) noexcept
{
    if (text.empty() || text.size() > SUBDOMAIN_CEILING) {
        return false;
    }

    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t dot = text.find('.', start);
        const std::size_t stop = dot == std::string_view::npos ? text.size() : dot;
        if (!isDnsLabel(text.substr(start, stop - start))) {
            return false;
        }
        if (dot == std::string_view::npos) {
            return true;
        }
        start = dot + 1;
    }
    return true;
}

std::expected<GroupVersionResource, ApiError>
GroupVersionResource::of(std::string group, std::string version, std::string resource)
{
    if (!group.empty() && !isDnsSubdomain(group)) {
        return std::unexpected(rejected("group is not a DNS-1123 subdomain: " + group));
    }
    if (!isDnsLabel(version)) {
        return std::unexpected(rejected("version is not a DNS-1123 label: " + version));
    }
    if (!isDnsLabel(resource)) {
        return std::unexpected(rejected("resource is not a DNS-1123 label: " + resource));
    }
    return GroupVersionResource{std::move(group), std::move(version), std::move(resource)};
}

std::string ApiPath::prefix() const
{
    if (type.isCore()) {
        return "/api/" + type.version;
    }
    return "/apis/" + type.group + "/" + type.version;
}

std::string ApiPath::render() const
{
    std::string path = prefix();
    if (namespaceName) {
        path += "/namespaces/";
        path += *namespaceName;
    }
    path += "/";
    path += type.resource;
    if (name) {
        path += "/";
        path += *name;
    }
    const std::string_view sub = subresourceName(subresource);
    if (!sub.empty()) {
        path += "/";
        path += sub;
    }
    return path;
}

std::expected<ApiPath, ApiError> ApiPath::inNamespace(std::string name) const
{
    if (!isDnsLabel(name)) {
        return std::unexpected(rejected("namespace is not a DNS-1123 label: " + name));
    }
    ApiPath copy = *this;
    copy.namespaceName = std::move(name);
    return copy;
}

std::expected<ApiPath, ApiError> ApiPath::named(std::string name) const
{
    if (!isPathSegment(name)) {
        return std::unexpected(rejected("name is not a path segment: " + name));
    }
    ApiPath copy = *this;
    copy.name = std::move(name);
    return copy;
}

ApiPath ApiPath::with(Subresource subresource) const
{
    ApiPath copy = *this;
    copy.subresource = subresource;
    return copy;
}

} // namespace antares::domain::k8s
