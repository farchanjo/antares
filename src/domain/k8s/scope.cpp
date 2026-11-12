#include "domain/k8s/scope.hpp"

#include <cstddef>

namespace antares::domain::k8s {

std::string_view scopeKindName(ScopeKind kind) noexcept
{
    switch (kind) {
    case ScopeKind::cluster:
        return "cluster";
    case ScopeKind::namespaced:
        return "namespace";
    }
    return "cluster";
}

std::size_t ScopeHash::operator()(const Scope &scope) const noexcept
{
    // FNV-1a over the three text parts. A scope is opened once and then hashed
    // per lookup, and it never exceeds a few hundred bytes.
    std::size_t hash = 1469598103934665603ULL;
    const auto fold = [&hash](std::string_view text) {
        for (const char character : text) {
            hash ^= static_cast<std::size_t>(static_cast<unsigned char>(character));
            hash *= 1099511628211ULL;
        }
        hash ^= std::size_t{0x1F};
        hash *= 1099511628211ULL;
    };

    fold(scope.cluster.text());
    fold(scope.type.group);
    fold(scope.type.version);
    fold(scope.type.resource);
    fold(scope.kind);
    fold(scope.namespaceName.text());
    return hash;
}

} // namespace antares::domain::k8s
