#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "adapters/k8s/cluster_binding.hpp"
#include "domain/http/client_config.hpp"
#include "domain/k8s/kubeconfig.hpp"
#include "ports/http/client.hpp"
#include "ports/k8s/kube_client.hpp"

namespace antares::adapters::k8s {

/// The `KubeClient` over the HTTP client port (FR-003 .. FR-011).
///
/// One instance serves one cluster: the authority, the credential and the TLS
/// material are fixed at construction, so a request cannot accidentally carry
/// another cluster's identity.
class HttpKubeClient final : public ports::k8s::KubeClient,
                             public std::enable_shared_from_this<HttpKubeClient> {
public:
    using Binding = ClusterBinding;

    /// Build a client from a resolved binding and an HTTP client already bound
    /// to the engine that carries the traffic.
    [[nodiscard]] static std::shared_ptr<HttpKubeClient>
    create(Binding binding, std::shared_ptr<ports::http::Client> http);

    [[nodiscard]] const domain::k8s::Cluster &cluster() const noexcept override;
    [[nodiscard]] const domain::k8s::Context &context() const noexcept override;

    [[nodiscard]] std::expected<std::shared_ptr<ports::k8s::Ticket>, domain::k8s::ApiError>
    get(const domain::k8s::ApiPath &path,
        std::function<void(ports::k8s::ObjectResult)> done) override;

    [[nodiscard]] std::expected<std::shared_ptr<ports::k8s::Ticket>, domain::k8s::ApiError>
    list(const domain::k8s::ApiPath &path, const domain::k8s::ListOptions &options,
         std::function<void(ports::k8s::ListResult)> done) override;

    [[nodiscard]] std::expected<std::shared_ptr<ports::k8s::Ticket>, domain::k8s::ApiError>
    create(const domain::k8s::ApiPath &path, domain::json::Value body,
           std::function<void(ports::k8s::ObjectResult)> done) override;

    [[nodiscard]] std::expected<std::shared_ptr<ports::k8s::Ticket>, domain::k8s::ApiError>
    update(const domain::k8s::ApiPath &path, domain::json::Value body,
           std::function<void(ports::k8s::ObjectResult)> done) override;

    [[nodiscard]] std::expected<std::shared_ptr<ports::k8s::Ticket>, domain::k8s::ApiError>
    patch(const domain::k8s::ApiPath &path, domain::k8s::PatchType type, std::string_view body,
          std::optional<std::string> fieldManager,
          std::function<void(ports::k8s::ObjectResult)> done) override;

    [[nodiscard]] std::expected<std::shared_ptr<ports::k8s::Ticket>, domain::k8s::ApiError>
    remove(const domain::k8s::ApiPath &path, const domain::k8s::DeleteOptions &options,
           std::function<void(ports::k8s::StatusResult)> done) override;

    [[nodiscard]] std::expected<std::shared_ptr<ports::k8s::Ticket>, domain::k8s::ApiError>
    watch(const domain::k8s::ApiPath &path, const domain::k8s::ListOptions &options,
          ports::k8s::WatchHandler handler,
          std::function<void(ports::k8s::StatusResult)> done) override;

    [[nodiscard]] std::expected<std::shared_ptr<ports::k8s::Ticket>, domain::k8s::ApiError>
    logs(const domain::k8s::ApiPath &path, const domain::k8s::ListOptions &options,
         std::function<void(std::string_view)> onLine,
         std::function<void(ports::k8s::StatusResult)> done) override;

    [[nodiscard]] std::expected<std::shared_ptr<ports::k8s::Ticket>, domain::k8s::ApiError>
    scale(const domain::k8s::ApiPath &path, std::optional<std::int32_t> replicas,
          std::function<void(ports::k8s::ObjectResult)> done) override;

    [[nodiscard]] ports::k8s::ClientStats stats() const noexcept override;

    [[nodiscard]] std::expected<std::shared_ptr<ports::k8s::Ticket>, domain::k8s::ApiError>
    serverVersion(std::function<void(ports::k8s::VersionResult)> done) override;

private:
    HttpKubeClient(Binding binding, std::shared_ptr<ports::http::Client> http);

    /// One request, already bound to this cluster: the URL, the credential and
    /// the accept type are attached here, never at a call site.
    [[nodiscard]] domain::http::Request request(domain::http::Method method,
                                                std::string_view path,
                                                std::string_view query) const;

    [[nodiscard]] std::expected<std::shared_ptr<ports::k8s::Ticket>, domain::k8s::ApiError>
    send(domain::http::Request request, std::function<void(ports::k8s::ObjectResult)> done);

    Binding binding_;
    std::shared_ptr<ports::http::Client> http_;
    mutable ports::k8s::ClientStats stats_;
};

} // namespace antares::adapters::k8s
