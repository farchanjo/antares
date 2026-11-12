#pragma once

#include <cstdint>
#include <string_view>

#include "adapters/ui/page_source.hpp"
#include "application/k8s/object_store.hpp"
#include "ports/k8s/kube_client.hpp"

namespace antares::adapters::ui {

/// A page source over the cluster.
///
/// It owns the half the model must not: it calls the API and writes the page
/// into the store, which publishes the batch as a `ChangeSet`. The order is the
/// contract — the batch is applied before `done` runs, so the rows exist by the
/// time the token settles and a scroll cannot ask for the same page twice.
class KubePageSource final : public PageSource {
public:
    KubePageSource(ports::k8s::Reader &reader, application::k8s::ObjectStore &store);
    ~KubePageSource() override;

    void fetch(const domain::k8s::Scope &scope, std::string_view continueToken,
               std::uint32_t limit, Done done) override;

private:
    ports::k8s::Reader &reader_;
    application::k8s::ObjectStore &store_;
};

} // namespace antares::adapters::ui
