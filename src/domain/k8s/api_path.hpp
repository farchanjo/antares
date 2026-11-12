#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "domain/k8s/api_error.hpp"

namespace antares::domain::k8s {

/// A subresource the client knows how to address (FR-001).
enum class Subresource : std::uint8_t { none, status, scale, log, exec };

[[nodiscard]] std::string_view subresourceName(Subresource subresource) noexcept;

/// A label or field selector, rendered as one query parameter.
struct Selector {
    std::string text;

    [[nodiscard]] bool empty() const noexcept { return text.empty(); }
};

/// What identifies one REST resource: the group, the version and the plural
/// resource name. The core group is the empty string.
///
/// The group and version are validated as DNS-1123 subdomains and the resource
/// as a DNS-1123 label, so a hostile name cannot traverse the built path
/// (FR-001, Security: input validation).
struct GroupVersionResource {
    std::string group;
    std::string version;
    std::string resource;

    [[nodiscard]] bool isCore() const noexcept { return group.empty(); }

    /// Builds a descriptor, refusing a name outside DNS-1123.
    [[nodiscard]] static std::expected<GroupVersionResource, ApiError>
    of(std::string group, std::string version, std::string resource);

    /// Identity, so a descriptor can key the store's scopes.
    friend bool operator==(const GroupVersionResource &, const GroupVersionResource &) noexcept =
        default;
};

/// Where a resource lives and what is addressed, rendered as one path (FR-001).
struct ApiPath {
    GroupVersionResource type;
    std::optional<std::string> namespaceName;
    std::optional<std::string> name;
    Subresource subresource = Subresource::none;

    /// `/api/v1` for the core group, `/apis/<group>/<version>` otherwise.
    [[nodiscard]] std::string prefix() const;

    /// The full path from the API prefix to the subresource. A namespaced
    /// resource carries `/namespaces/<name>`, a named one the object name, and
    /// a subresource its own segment.
    [[nodiscard]] std::string render() const;

    /// Same value with a namespace attached; refuses a name outside DNS-1123.
    [[nodiscard]] std::expected<ApiPath, ApiError> inNamespace(std::string name) const;

    /// Same value addressing one object.
    [[nodiscard]] std::expected<ApiPath, ApiError> named(std::string name) const;

    [[nodiscard]] ApiPath with(Subresource subresource) const;
};

/// True when `text` is a DNS-1123 label: lowercase alphanumeric or `-`, at most
/// 63 characters, starting and ending alphanumeric.
[[nodiscard]] bool isDnsLabel(std::string_view text) noexcept;

/// True when `text` is a DNS-1123 subdomain: dot-separated labels, at most 253
/// characters.
[[nodiscard]] bool isDnsSubdomain(std::string_view text) noexcept;

} // namespace antares::domain::k8s
