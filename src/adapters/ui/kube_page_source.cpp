#include "adapters/ui/kube_page_source.hpp"

#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "domain/k8s/api_path.hpp"
#include "domain/k8s/options.hpp"
#include "adapters/ui/page_error.hpp"
#include "domain/k8s/watch_event.hpp"

namespace antares::adapters::ui {

KubePageSource::KubePageSource(ports::k8s::Reader &reader, application::k8s::ObjectStore &store)
    : reader_(reader)
    , store_(store)
{
}

KubePageSource::~KubePageSource() = default;

void KubePageSource::fetch(const domain::k8s::Scope &scope, std::string_view continueToken,
                           std::uint32_t limit, Done done)
{
    std::fprintf(stderr, "  [fetch] ns=%s res=%s\n", scope.namespaceName.text().c_str(),
                 scope.type.resource.c_str());
    const domain::k8s::ApiPath path{scope.type, std::nullopt, std::nullopt,
                                    domain::k8s::Subresource::none};
    // Opening is idempotent: a second page of the same scope gets the open one
    // back. Without it `apply` has nowhere to write and the page is dropped.
    if (auto opened = store_.open(scope); !opened) {
        done(std::unexpected(PageError::unknownScope));
        return;
    }

    domain::k8s::ListOptions options;
    options.limit = limit;
    if (!continueToken.empty()) {
        options.continueToken = std::string{continueToken};
    }

    // The completion may run on the loop. The batch is applied here and the
    // store publishes the change set, so the rows are in place before the model
    // is told the page landed.
    auto shared = std::make_shared<domain::k8s::Scope>(scope);
    static_cast<void>(reader_.list(
        path, options,
        [this, shared, done = std::move(done)](ports::k8s::ListResult result) {
            if (!result) {
                std::fprintf(stderr, "  [page] code=%u %s\n", result.error().code,
                             result.error().message.c_str());
                done(std::unexpected(PageError::transport));
                return;
            }
            std::fprintf(stderr, "  [page] items=%zu token=%zu\n", result->items.size(),
                         result->continueToken.size());

            std::vector<domain::k8s::WatchEvent> batch;
            batch.reserve(result->items.size());
            for (auto &entry : result->items) {
                domain::k8s::WatchEvent event;
                event.type = domain::k8s::EventType::added;
                event.resourceVersion = result->resourceVersion;
                event.entry = std::move(entry);
                batch.push_back(std::move(event));
            }
            const auto applied = store_.apply(*shared, batch);
            std::fprintf(stderr, "  [page] aplicado inseridos=%zu\n", applied.inserted.size());

            done(result->continueToken);
        }));
}

} // namespace antares::adapters::ui
