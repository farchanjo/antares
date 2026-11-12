#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "domain/k8s/api_path.hpp"
#include "domain/k8s/cluster_name.hpp"
#include "domain/k8s/namespace_name.hpp"

namespace antares::domain::k8s {

/// The two values the `scope_kind` metric label may take (FR-004, D-15).
enum class ScopeKind : std::uint8_t { cluster, namespaced };

[[nodiscard]] std::string_view scopeKindName(ScopeKind kind) noexcept;

/// What one store scope is: the cluster, the resource and the namespace.
///
/// The namespace is empty for a cluster-scoped resource, and `ScopeKind` is
/// derived from it rather than stored twice. The kind of the resource is part
/// of the scope too — one group-version-resource serves exactly one kind — so
/// neither the namespace nor the kind is repeated in every entry.
struct Scope {
    ClusterName cluster;
    GroupVersionResource type;
    std::string kind;
    NamespaceName namespaceName;

    [[nodiscard]] ScopeKind scopeKind() const noexcept
    {
        return namespaceName.empty() ? ScopeKind::cluster : ScopeKind::namespaced;
    }

    friend bool operator==(const Scope &, const Scope &) noexcept = default;
};

struct ScopeHash {
    [[nodiscard]] std::size_t operator()(const Scope &scope) const noexcept;
};

} // namespace antares::domain::k8s
