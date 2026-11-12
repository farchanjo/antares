#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <thread>

#include "adapters/ui/change_queue.hpp"
#include "adapters/ui/model_sink.hpp"
#include "adapters/ui/model_wakeup.hpp"
#include "adapters/ui/page_source.hpp"
#include "adapters/ui/row_role.hpp"
#include "application/k8s/object_store.hpp"
#include "application/k8s/store_scope.hpp"
#include "domain/k8s/change_set.hpp"
#include "domain/k8s/scope.hpp"
#include "ports/log/logger.hpp"

namespace antares::adapters::ui {

/// The list, as a lens over the store (FR-001).
///
/// The model owns no row: `rowCount` is the scope's live entry count plus the
/// retry row, and `data`/`multiData` resolve a row to the stored entry. Five
/// thousand rows therefore cost the `sizeof` of this class and nothing else,
/// which is the case `model-footprint-under-4k` asserts.
///
/// Confinement (FR-008): every mutation runs on the thread that constructed the
/// model. A batch from anywhere else is queued by `applyLater` and the consumer
/// is woken; `drain` replays the queue in order on the model's thread. A page
/// completion rides the same queue, so its rows are always inserted before its
/// token is stored.
///
/// Ordering (FR-005): the store keeps arrival order, so a position only moves
/// when the ordering key changes. The row is re-placed in the store and the view
/// is told `rowsMoved`, never a remove plus an insert, so the selection and the
/// scroll offset survive.
class ResourceListModel {
public:
    /// Objects one page asks for (FR-002).
    static constexpr std::uint32_t pageLimit = 500;

    /// The namespace is empty for a cluster-scoped resource, and the store still
    /// keys on it, so the scope is copied rather than referenced.
    /// `continueToken` is the token the first page left behind: the window opens
    /// the scope, fetches the first page and only then builds the model, so the
    /// token is an input and not something the model invents.
    ResourceListModel(application::k8s::ObjectStore &store, domain::k8s::Scope scope,
                      PageSource &pages, ModelSink &sink, ModelWakeup &wakeup,
                      PageToken continueToken = {}, ports::log::Logger *logger = nullptr);

    ResourceListModel(const ResourceListModel &) = delete;
    ResourceListModel &operator=(const ResourceListModel &) = delete;
    ResourceListModel(ResourceListModel &&) = delete;
    ResourceListModel &operator=(ResourceListModel &&) = delete;
    ~ResourceListModel() = default;

    [[nodiscard]] const domain::k8s::Scope &scope() const noexcept { return scope_; }

    /// Stored rows, plus the retry row while a fetch has failed.
    [[nodiscard]] std::uint32_t rowCount() const noexcept;

    /// The text of one role, or nothing when the row is past the end.
    [[nodiscard]] std::optional<std::string> data(std::uint32_t row, RowRole role) const;

    /// Fill `roles.size()` texts in one call (FR-003). A row past the end fills
    /// empty strings, so a delegate never re-indexes.
    void multiData(std::uint32_t row, std::span<const RowRole> roles,
                   std::span<std::string> texts) const;

    /// True while the store holds a `continue` token and no page is in flight.
    /// False while the retry row is shown, so the view does not retry in a loop.
    [[nodiscard]] bool canFetchMore() const noexcept;

    /// Ask the source for the next page. The insert happens when the page
    /// settles, inside one range.
    void fetchMore();

    /// Drop the retry row and ask again. The only way a failure is retried.
    void retry();

    /// Replay one batch as row operations (FR-004, FR-005). Removals descend,
    /// insertions ascend, changes follow, moves last: the order Qt requires and
    /// the only one under which the set's indices stay valid while it is replayed.
    void apply(const domain::k8s::ChangeSet &changes);

    /// Queue a batch produced on another thread and wake the model's thread.
    void applyLater(domain::k8s::ChangeSet changes);

    /// Replay everything queued, oldest first, on the model's thread. Called by
    /// the wakeup; a test calls it directly.
    void drain();

    /// The scope hit an admission ceiling: the list is not the whole answer.
    [[nodiscard]] bool truncated() const noexcept;
    [[nodiscard]] std::uint64_t refused() const noexcept;

    /// True while the retry row is shown.
    [[nodiscard]] bool failed() const noexcept { return failure_.has_value(); }

    /// The model's own footprint, which no row adds to (FR-001).
    [[nodiscard]] std::size_t footprintBytes() const noexcept { return sizeof(*this); }

private:
    [[nodiscard]] const application::k8s::StoreScope *stored() const noexcept;
    [[nodiscard]] std::uint32_t storedRows() const noexcept;

    void onPage(PageOutcome outcome);
    void settlePage(PageOutcome outcome);
    void showRetry(PageError error);
    void hideRetry();
    void moveToTail(std::uint32_t row);
    void logFetchFailure(PageError error) const;

    application::k8s::ObjectStore &store_;
    domain::k8s::Scope scope_;
    PageSource &pages_;
    ModelSink &sink_;
    ModelWakeup &wakeup_;
    ports::log::Logger *logger_ = nullptr;
    ChangeQueue queue_;
    PageToken continueToken_;
    std::thread::id owner_ = std::this_thread::get_id();
    bool fetching_ = false;
    bool started_ = false;
    std::optional<PageError> failure_;
};

} // namespace antares::adapters::ui
