// Read-only smoke for the Kubernetes client over the project's own HTTP client.
//
// This is the tool that answers one question: does our transport actually speak
// to a real API server? It loads ~/.kube, resolves one context into a binding,
// and exercises only the reading surface — version, namespaces, a short watch.
//
// It never mutates. The type it holds is `ports::k8s::Reader`, so there is no
// write verb to call by accident while the client is being tuned.
//
// Exit codes: 0 all steps passed, 1 a step failed, 2 bad usage.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "adapters/http/client.hpp"
#include "adapters/k8s/cluster_binding.hpp"
#include "adapters/k8s/http_kube_client.hpp"
#include "adapters/k8s/kubeconfig_loader.hpp"
#include "adapters/log/console_logger.hpp"
#include "application/io/engine_pool.hpp"
#include "domain/k8s/api_path.hpp"
#include "ports/k8s/kube_client.hpp"

namespace adapters = antares::adapters;
namespace application = antares::application;
namespace domain = antares::domain;
namespace k8s = antares::domain::k8s;
namespace ports = antares::ports;

namespace {

/// A latch the engine thread opens, so the tool never spins or sleeps.
class Gate {
public:
    void open()
    {
        {
            const std::lock_guard<std::mutex> guard{mutex_};
            open_ = true;
        }
        cv_.notify_all();
    }

    [[nodiscard]] bool wait(std::chrono::seconds budget)
    {
        std::unique_lock<std::mutex> lock{mutex_};
        return cv_.wait_for(lock, budget, [this] { return open_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool open_ = false;
};

int failures = 0;      // NOLINT — mutable, shared by every step
bool stalled = false;  // NOLINT

void step(bool ok, const std::string &what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) {
        ++failures;
    }
}

void listContexts(const k8s::Kubeconfig &config)
{
    std::printf("\ncontexts (%zu):\n", config.contexts.size());
    for (const k8s::Context &context : config.contexts) {
        const k8s::Cluster *cluster = config.findCluster(context.cluster);
        const k8s::User *user = config.findUser(context.user);
        const char *auth = "none";
        if (user != nullptr) {
            if (!user->token.empty()) {
                auth = "token";
            } else if (!user->clientCertificatePem.empty()) {
                auth = "mtls";
            }
        }
        std::printf("  %-24s cluster=%-16s auth=%-6s %s%s\n", context.name.c_str(),
                    context.cluster.c_str(), auth,
                    cluster == nullptr ? "<no cluster>" : cluster->server.c_str(),
                    context.name == config.currentContext ? "  (current)" : "");
    }
    std::printf("\n");
}

/// The full reading slice against one context.
int smoke(const std::string &contextName, const std::string &namespaceOverride,
          const std::vector<std::string> &paths)
{
    auto config = adapters::k8s::KubeconfigLoader::load(paths);
    if (!config) {
        std::printf("FAIL  kubeconfig: %s\n",
                    std::string{k8s::describe(config.error())}.c_str());
        return 1;
    }
    listContexts(*config);

    const std::string wanted = contextName.empty() ? config->currentContext : contextName;
    auto binding = adapters::k8s::ClusterBinding::resolve(*config, wanted);
    if (!binding) {
        std::printf("FAIL  binding: %s\n",
                    std::string{k8s::describe(binding.error())}.c_str());
        return 1;
    }

    const char *kind = "anonymous";
    if (binding->http.auth.kind == domain::http::AuthKind::bearer) {
        kind = "bearer";
    } else if (binding->http.auth.kind == domain::http::AuthKind::clientCertificate) {
        kind = "mTLS";
    }
    std::printf("binding: context=%s base=%s auth=%s ca=%zu bytes verify=%s\n",
                binding->context.name.c_str(), binding->base.c_str(), kind,
                binding->http.trustBundlePem.size(),
                binding->http.verifyPeer ? "on" : "off");

    adapters::log::ConsoleLogger logger;
    binding->http.logger = &logger;

    // The context usually names no namespace, and `default` is the API server's
    // own answer for that; the override exists so a populated namespace can be
    // exercised without editing the kubeconfig.
    const std::string ns =
        namespaceOverride.empty() ? binding->defaultNamespace : namespaceOverride;

    application::io::EnginePool pool{1};
    pool.start();

    auto http = std::make_shared<adapters::http::HttpClient>(pool, binding->http);
    auto client = adapters::k8s::HttpKubeClient::create(*binding, http);

    // The type the rest of the tool sees: reading only.
    ports::k8s::Reader &reader = *client;

    std::printf("\n");

    {
        Gate gate;
        static_cast<void>(reader.serverVersion([&](ports::k8s::VersionResult result) {
            if (result) {
                step(true, "GET /version  " + result->gitVersion + "  (" + result->platform + ")");
            } else {
                step(false, "GET /version  " + std::string{result.error().name()} + ": " +
                                result.error().message);
            }
            gate.open();
        }));
        if (!gate.wait(std::chrono::seconds{20})) {
            step(false, "GET /version  timed out");
            stalled = true;
        }
    }

    {
        Gate gate;
        auto namespaceType = k8s::GroupVersionResource::of("", "v1", "namespaces");
        if (!namespaceType) {
            step(false, "namespaces descriptor");
        } else {
            const k8s::ApiPath path{*namespaceType, std::nullopt, std::nullopt, k8s::Subresource::none};
            k8s::ListOptions options;
            options.limit = 5;
            static_cast<void>(reader.list(path, options, [&](ports::k8s::ListResult result) {
                if (result) {
                    step(true, "GET /api/v1/namespaces  " + std::to_string(result->items.size()) +
                                   " visible (limit 5)");
                } else {
                    step(false, "GET /api/v1/namespaces  " +
                                    std::string{result.error().name()} + ": " +
                                    result.error().message);
                }
                gate.open();
            }));
            if (!gate.wait(std::chrono::seconds{20})) {
                step(false, "GET /api/v1/namespaces  timed out");
                stalled = true;
            }
        }
    }

    {
        Gate gate;
        auto podType = k8s::GroupVersionResource::of("", "v1", "pods");
        if (!podType) {
            step(false, "pods descriptor");
        } else {
            auto scoped = k8s::ApiPath{*podType, std::nullopt, std::nullopt, k8s::Subresource::none}
                              .inNamespace(ns);
            if (!scoped) {
                step(false, "namespace is not a DNS label: " + ns);
            } else {
                k8s::ListOptions options;
                options.watch = true;
                options.timeoutSeconds = 5;

                std::atomic<int> seen{0};
                ports::k8s::WatchHandler handler;
                handler.onEvent = [&](const k8s::WatchEvent &event) {
                    if (event.produces()) {
                        seen.fetch_add(1);
                    }
                };
                static_cast<void>(reader.watch(*scoped, options, handler,
                                   [&](ports::k8s::StatusResult result) {
                                       if (result) {
                                           step(true, "WATCH pods/" +
                                                          ns + "  " +
                                                          std::to_string(seen.load()) +
                                                          " object(s)");
                                       } else if (result.error().reason == k8s::Reason::timeout) {
                                           step(true, "WATCH ended by the server timeout");
                                       } else {
                                           step(false, "WATCH  " +
                                                           std::string{result.error().name()} +
                                                           ": " + result.error().message);
                                       }
                                       gate.open();
                                   }));
                // The server honours timeoutSeconds; the wait is the backstop.
                if (!gate.wait(std::chrono::seconds{25})) {
                    step(false, "WATCH  timed out locally");
                    stalled = true;
                }
            }
        }
    }

    const ports::k8s::ClientStats stats = reader.stats();
    std::printf("\nstats: submitted=%llu ok=%llu failed=%llu events=%llu bookmarks=%llu\n",
                static_cast<unsigned long long>(stats.submitted),
                static_cast<unsigned long long>(stats.succeeded),
                static_cast<unsigned long long>(stats.failed),
                static_cast<unsigned long long>(stats.watchEvents),
                static_cast<unsigned long long>(stats.bookmarks));

    http.reset();
    if (stalled) {
        // A stuck resolve blocks the loop thread inside getaddrinfo, and there
        // is no way to interrupt it. Joining would hang the tool forever, which
        // is the opposite of what a diagnostic is for.
        std::printf("\nstalled: a step never completed, skipping the clean shutdown\n");
        std::fflush(stdout);
        std::_Exit(1);
    }
    pool.stop();
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    // A redirected stdout is block-buffered, which hides a hang behind an empty
    // log. Line buffering keeps the trace honest while the client is tuned.
    (void)std::setvbuf(stdout, nullptr, _IOLBF, 0);

    std::string context;
    std::string nsArg;
    std::vector<std::string> paths;

    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--context") == 0 && index + 1 < argc) {
            context = argv[++index];
            continue;
        }
        if (std::strcmp(argv[index], "--namespace") == 0 && index + 1 < argc) {
            nsArg = argv[++index];
            continue;
        }
        if (std::strcmp(argv[index], "--kubeconfig") == 0 && index + 1 < argc) {
            paths.emplace_back(argv[++index]);
            continue;
        }
        if (std::strcmp(argv[index], "--help") == 0) {
            std::printf(
                "usage: antares_k8s_live [--context NAME] [--namespace NS] [--kubeconfig PATH]...\n"
                "  Reads a kubeconfig, resolves one context and exercises only:\n"
                "    GET /version, GET /api/v1/namespaces, WATCH pods/<namespace>\n"
                "  It never writes. There is no verb to write with.\n");
            return 0;
        }
        std::printf("unknown argument: %s\n", argv[index]);
        return 2;
    }

    return smoke(context, nsArg, paths);
}
