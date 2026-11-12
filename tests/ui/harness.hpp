#pragma once

#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "adapters/ui/resource_list_model.hpp"
#include "application/k8s/object_store.hpp"
#include "domain/k8s/change_set.hpp"
#include "domain/k8s/entry.hpp"
#include "domain/k8s/scope.hpp"
#include "domain/k8s/uid.hpp"
#include "domain/k8s/watch_event.hpp"
#include "fake_page_source.hpp"
#include "recording_logger.hpp"
#include "recording_sink.hpp"
#include "recording_wakeup.hpp"

namespace antares::test::ui {

using adapters::ui::RowRole;
using domain::k8s::ChangeSet;
using domain::k8s::Entry;
using domain::k8s::EventType;
using domain::k8s::Scope;
using domain::k8s::Uid;
using domain::k8s::WatchEvent;

inline int failures = 0;

inline void check(bool condition, std::string_view what)
{
    if (condition) {
        return;
    }
    std::printf("  FAIL %.*s\n", static_cast<int>(what.size()), what.data());
    ++failures;
}

inline void checkEqual(std::string_view actual, std::string_view expected, std::string_view what)
{
    if (actual == expected) {
        return;
    }
    std::printf("  FAIL %.*s: got '%.*s', wanted '%.*s'\n", static_cast<int>(what.size()),
                what.data(), static_cast<int>(actual.size()), actual.data(),
                static_cast<int>(expected.size()), expected.data());
    ++failures;
}

[[nodiscard]] inline std::string uidText(std::size_t index)
{
    char buffer[40] = {};
    std::snprintf(buffer, sizeof(buffer), "00000000-0000-4000-8000-%012zx", index);
    return std::string{buffer};
}

[[nodiscard]] inline std::string nameFor(std::size_t index)
{
    return "pod-" + std::to_string(index);
}

[[nodiscard]] inline Scope scopeFor(std::string_view namespaceName = "shop")
{
    Scope scope;
    scope.cluster = domain::k8s::ClusterName::of("dev").value_or(domain::k8s::ClusterName{});
    scope.type = domain::k8s::GroupVersionResource::of("", "v1", "pods").value_or(
        domain::k8s::GroupVersionResource{});
    scope.kind = "Pod";
    scope.namespaceName =
        domain::k8s::NamespaceName::of(namespaceName).value_or(domain::k8s::NamespaceName{});
    return scope;
}

[[nodiscard]] inline std::optional<Entry> entryFor(std::size_t index)
{
    const auto uid = Uid::parse(uidText(index));
    if (!uid) {
        return std::nullopt;
    }
    Entry entry;
    entry.identity.uid = *uid;
    entry.identity.name = nameFor(index);
    entry.resourceVersion = "184223";
    entry.generation = 1;
    entry.creationTimestamp = "2026-11-11T19:05:00Z";
    entry.status.reason = "Running";
    return entry;
}

[[nodiscard]] inline WatchEvent eventOf(EventType type, const Entry &entry)
{
    WatchEvent event;
    event.type = type;
    event.resourceVersion = entry.resourceVersion;
    event.entry = entry;
    return event;
}

/// The three parties a model needs, wired the way the window will wire them.
///
/// The store's listener posts into the model exactly as the production wiring
/// does, so a case that goes through the store exercises the crossing and not a
/// shortcut.
class Harness {
public:
    explicit Harness(domain::k8s::StoreBudget budget = {}) : store(budget) {}
    Harness(const Harness &) = delete;
    Harness &operator=(const Harness &) = delete;
    Harness(Harness &&) = delete;
    Harness &operator=(Harness &&) = delete;
    ~Harness() { store.setListener({}); }

    /// Open the scope and build the model over it. The token is what the first
    /// page left behind; an empty one means the server has no next page.
    void build(std::string_view namespaceName = "shop", std::string_view continueToken = "start")
    {
        store.setListener([this](const ChangeSet &changes) {
            if (model) {
                model->applyLater(changes);
            }
        });
        const auto opened = store.open(scopeFor(namespaceName));
        check(opened.has_value(), "scope opens");
        model = std::make_unique<adapters::ui::ResourceListModel>(store, scopeFor(namespaceName),
                                                                  pages, sink, wakeup,
                                                                  std::string{continueToken},
                                                                  &logger);
        wakeup.onWake = [this] {
            if (model) {
                model->drain();
            }
        };
    }

    /// Store `count` fixture pods through one batch.
    void seed(std::size_t count)
    {
        std::vector<WatchEvent> batch;
        batch.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto entry = entryFor(index);
            check(entry.has_value(), "fixture entry builds");
            if (!entry) {
                return;
            }
            batch.push_back(eventOf(EventType::added, *entry));
        }
        static_cast<void>(store.apply(scopeFor(), batch));
    }

    /// A page of `count` entries starting at `from`.
    [[nodiscard]] std::vector<Entry> page(std::size_t from, std::size_t count)
    {
        std::vector<Entry> entries;
        entries.reserve(count);
        for (std::size_t index = from; index < from + count; ++index) {
            const auto entry = entryFor(index);
            if (entry) {
                entries.push_back(*entry);
            }
        }
        return entries;
    }

    application::k8s::ObjectStore store;
    RecordingSink sink;
    RecordingWakeup wakeup;
    FakePageSource pages{store};
    RecordingLogger logger;
    std::unique_ptr<adapters::ui::ResourceListModel> model;
};

} // namespace antares::test::ui
