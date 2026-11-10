#include "adapters/k8s/cluster_binding.hpp"

#include "domain/http/url.hpp"

namespace antares::adapters::k8s {

namespace {

/// `https://host:6443/` becomes `https://host:6443`, so a path built later
/// never doubles a slash.
[[nodiscard]] std::string normalizeBase(std::string_view server)
{
    std::string base{server};
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base;
}

/// Pick the credential the kubeconfig actually carries. A token wins over a
/// certificate only because a file may hold both and the token is the cheaper
/// handshake; a user with neither is anonymous, which is legal.
[[nodiscard]] domain::http::Auth authFor(const domain::k8s::User &user)
{
    if (!user.token.empty()) {
        return domain::http::Auth::bearer(user.token);
    }
    if (!user.clientCertificatePem.empty() && !user.clientKeyPem.empty()) {
        return domain::http::Auth::clientCertificate(user.clientCertificatePem,
                                                     user.clientKeyPem);
    }
    return {};
}

} // namespace

std::expected<ClusterBinding, domain::k8s::ConfigError>
ClusterBinding::resolve(const domain::k8s::Kubeconfig &config, std::string_view contextName)
{
    auto context = config.select(contextName);
    if (!context) {
        return std::unexpected(context.error());
    }

    const domain::k8s::Cluster *cluster = config.findCluster(context->cluster);
    if (cluster == nullptr) {
        return std::unexpected(domain::k8s::ConfigError::unknownCluster);
    }

    const domain::k8s::User *user = config.findUser(context->user);
    if (user == nullptr) {
        // A context may name no user at all, which is an anonymous context.
        static const domain::k8s::User anonymous{};
        user = &anonymous;
    }

    if (cluster->server.empty()) {
        return std::unexpected(domain::k8s::ConfigError::noServer);
    }

    ClusterBinding binding;
    binding.cluster = *cluster;
    binding.context = *context;
    binding.user = *user;
    binding.base = normalizeBase(cluster->server);
    binding.defaultNamespace =
        context->defaultNamespace.empty() ? "default" : context->defaultNamespace;

    binding.http.verifyPeer = !cluster->insecureSkipTlsVerify;
    // The cluster's own anchors replace the system store: a managed cluster
    // signs with a private CA that the system has never heard of.
    binding.http.trustBundlePem = cluster->certificateAuthorityPem;
    binding.http.auth = authFor(*user);
    binding.http.userAgent = "antares/0.1";
    return binding;
}

} // namespace antares::adapters::k8s
