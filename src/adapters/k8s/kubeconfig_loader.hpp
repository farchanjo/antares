#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "domain/k8s/kubeconfig.hpp"

namespace antares::adapters::k8s {

/// Reads kubeconfig documents from disk and turns them into one `Kubeconfig`.
///
/// The search order is the one `kubectl` uses: an explicit list, then
/// `KUBECONFIG` (colon-separated), then `$HOME/.kube/config`. When several
/// documents load, they merge with the first entry winning, so the file a user
/// names first is the one that decides.
class KubeconfigLoader {
public:
    /// Load and merge. An empty list means "use the search order".
    [[nodiscard]] static std::expected<domain::k8s::Kubeconfig, domain::k8s::ConfigError>
    load(const std::vector<std::string> &paths = {});

    /// The paths the search order would read, whether or not they exist.
    [[nodiscard]] static std::vector<std::string> searchPaths();

    /// True when at least one candidate file exists.
    [[nodiscard]] static bool present();

    /// Read every file the document points at into its inline field: a
    /// certificate-authority path, a client certificate, a key, a token file.
    ///
    /// A kubeconfig may describe a credential by reference. Resolving those
    /// here keeps the binding pure and keeps the failure at load time, where a
    /// message can still name the file.
    static void hydrate(domain::k8s::Kubeconfig &config);
};

/// Merge documents, first entry winning on a name collision.
[[nodiscard]] domain::k8s::Kubeconfig merge(const std::vector<domain::k8s::Kubeconfig> &documents);

/// Resolve the credential of one user (FR-007): read a token file, and refuse
/// what cannot be resolved without running something.
class FileCredentialSource {
public:
    [[nodiscard]] static std::expected<domain::k8s::User, domain::k8s::ConfigError>
    resolve(const domain::k8s::User &user);
};

} // namespace antares::adapters::k8s
