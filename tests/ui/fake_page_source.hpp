#pragma once

#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "adapters/ui/page_source.hpp"
#include "application/k8s/object_store.hpp"
#include "domain/k8s/entry.hpp"
#include "domain/k8s/watch_event.hpp"

namespace antares::test::ui {

/// A page source with no network: it writes the queued page into the store,
/// which publishes the batch exactly as the real one does, and then settles the
/// token. A queued failure settles without writing.
class FakePageSource : public adapters::ui::PageSource {
public:
    explicit FakePageSource(application::k8s::ObjectStore &store) : store_(&store) {}

    void queuePage(std::vector<domain::k8s::Entry> entries, std::string token)
    {
        Pending pending;
        pending.entries = std::move(entries);
        pending.token = std::move(token);
        pending_.push_back(std::move(pending));
    }

    void queueFailure(adapters::ui::PageError error)
    {
        Pending pending;
        pending.error = error;
        pending_.push_back(std::move(pending));
    }

    void fetch(const domain::k8s::Scope &scope, std::string_view continueToken, std::uint32_t limit,
               Done done) override
    {
        ++fetches;
        lastLimit = limit;
        lastToken = std::string{continueToken};

        if (pending_.empty()) {
            done(std::unexpected(adapters::ui::PageError::transport));
            return;
        }
        Pending pending = std::move(pending_.front());
        pending_.pop_front();
        if (pending.error) {
            done(std::unexpected(*pending.error));
            return;
        }

        std::vector<domain::k8s::WatchEvent> batch;
        batch.reserve(pending.entries.size());
        for (const domain::k8s::Entry &entry : pending.entries) {
            domain::k8s::WatchEvent event;
            event.type = domain::k8s::EventType::added;
            event.resourceVersion = entry.resourceVersion;
            event.entry = entry;
            batch.push_back(std::move(event));
        }
        // The store publishes the batch before `done` runs, which is the
        // contract the real source has to keep as well.
        static_cast<void>(store_->apply(scope, batch));
        done(adapters::ui::PageToken{std::move(pending.token)});
    }

    std::size_t fetches = 0;
    std::uint32_t lastLimit = 0;
    std::string lastToken;

private:
    struct Pending {
        std::vector<domain::k8s::Entry> entries;
        std::string token;
        std::optional<adapters::ui::PageError> error;
    };

    application::k8s::ObjectStore *store_;
    std::deque<Pending> pending_;
};

} // namespace antares::test::ui
