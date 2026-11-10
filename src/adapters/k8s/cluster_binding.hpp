#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "domain/http/client_config.hpp"
#include "domain/k8s/kubeconfig.hpp"

namespace antares::adapters::k8s {

/// Everything one cluster connection needs, resolved once: where to dial, how
/// to verify the peer, how to authenticate.
///
/// Built from a kubeconfig and a context name, never at a call site, so a
/// request cannot end up carrying another cluster's identity — the failure mode
/// that is easiest to cause and hardest to see.
struct ClusterBinding {
    domain::k8s::Cluster cluster;
    domain::k8s::Context context;
    domain::k8s::User user;

    /// `scheme://host[:port]` of the API server, without a trailing path.
    std::string base;

    /// What the HTTP client needs for this cluster: the credential and the
    /// trust anchors. The caller passes this to the client constructor.
    domain::http::ClientConfig http;

    /// The namespace a request defaults to when the caller names none.
    std::string defaultNamespace;

    /// Resolve `contextName` out of `config`. An empty name means the current
    /// context. The user's file-backed credentials must already be inlined, so
    /// this touches no filesystem and cannot fail for an I/O reason.
    [[nodiscard]] static std::expected<ClusterBinding, domain::k8s::ConfigError>
    resolve(const domain::k8s::Kubeconfig &config, std::string_view contextName = {});
};

} // namespace antares::adapters::k8s
