#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "domain/k8s/kubeconfig.hpp"

namespace antares::adapters::k8s {

/// Reads a kubeconfig from disk and parses it (FR-006).
///
/// The search order is the one `kubectl` uses: an explicit path, then
/// `KUBECONFIG`, then `$HOME/.kube/config`. A file that exists but does not
/// parse is `malformed`; a path that does not resolve is `missingFile`, so the
/// UI can offer to pick one instead of showing a parse error.
class KubeconfigLoader {
public:
    [[nodiscard]] static std::expected<domain::k8s::Kubeconfig, domain::k8s::ConfigError>
    load(std::string_view explicitPath = {});

    /// True when a kubeconfig exists at the default location for the current
    /// user. Cheap enough to call before offering a cluster list.
    [[nodiscard]] static bool present();
};

/// Resolve the credential of one user (FR-007). Reads a token file or takes the
/// embedded value; a user that needs an exec plugin reports it as unresolved
/// rather than guessing.
class FileCredentialSource {
public:
    [[nodiscard]] static std::expected<domain::k8s::User, domain::k8s::ConfigError>
    resolve(const domain::k8s::User &user);
};

} // namespace antares::adapters::k8s
