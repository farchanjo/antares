#include "adapters/ui/resource_list_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>

#include "adapters/ui/page_error.hpp"
#include "adapters/ui/row_text.hpp"
#include "domain/k8s/entry.hpp"

namespace antares::adapters::ui {

using domain::k8s::ChangedRow;
using domain::k8s::ChangeSet;
using domain::k8s::RowRange;

namespace {

/// The retry row, so a delegate and the case agree on the two strings it shows.
constexpr std::string_view RETRY_NAME = "retry";

} // namespace

ResourceListModel::ResourceListModel(application::k8s::ObjectStore &store,
                                     domain::k8s::Scope scope, PageSource &pages,
                                     ModelSink &sink, ModelWakeup &wakeup,
                                     PageToken continueToken, ports::log::Logger *logger)
    : store_(store), scope_(std::move(scope)), pages_(pages), sink_(sink), wakeup_(wakeup),
      continueToken_(std::move(continueToken)), logger_(logger)
{
}

const application::k8s::StoreScope *ResourceListModel::stored() const noexcept
{
    return store_.find(scope_);
}

std::uint32_t ResourceListModel::storedRows() const noexcept
{
    const application::k8s::StoreScope *scope = stored();
    if (scope == nullptr) {
        return 0;
    }
    return static_cast<std::uint32_t>(scope->size());
}

std::uint32_t ResourceListModel::rowCount() const noexcept
{
    return storedRows() + (failure_ ? 1U : 0U);
}

std::optional<std::string> ResourceListModel::data(std::uint32_t row, RowRole role) const
{
    const std::uint32_t rows = storedRows();
    if (row < rows) {
        const application::k8s::StoreScope *scope = stored();
        if (scope == nullptr) {
            return std::nullopt;
        }
        return rowText(scope->at(row), scope_, role);
    }
    if (row == rows && failure_) {
        if (role == RowRole::name) {
            return std::string{RETRY_NAME};
        }
        if (role == RowRole::status) {
            return std::string{pageErrorName(*failure_)};
        }
        return std::string{};
    }
    return std::nullopt;
}

void ResourceListModel::multiData(std::uint32_t row, std::span<const RowRole> roles,
                                  std::span<std::string> texts) const
{
    for (std::size_t index = 0; index < roles.size() && index < texts.size(); ++index) {
        texts[index] = data(row, roles[index]).value_or(std::string{});
    }
}

bool ResourceListModel::canFetchMore() const noexcept
{
    if (fetching_ || failure_) {
        return false;
    }
    // The first page has no token yet, so an empty one only stops the loop after
    // a page has landed and returned none.
    return !started_ || !continueToken_.empty();
}

void ResourceListModel::fetchMore()
{
    if (!canFetchMore()) {
        return;
    }
    fetching_ = true;
    started_ = true;
    // The completion may run on any thread; `onPage` queues and wakes, so the
    // token is only ever stored on the model's own thread.
    pages_.fetch(scope_, continueToken_, pageLimit,
                 [this](PageOutcome outcome) { onPage(std::move(outcome)); });
}

void ResourceListModel::retry()
{
    if (!failure_) {
        return;
    }
    hideRetry();
    // `hideRetry` notifies the view, which may itself call `fetchMore` before
    // this line runs. Whichever asks first sets `fetching_`, so only one page is
    // ever in flight.
    fetchMore();
}

void ResourceListModel::apply(const ChangeSet &changes)
{
    if (stored() == nullptr) {
        return;
    }

    for (const RowRange &range : changes.removed) {
        sink_.rowsRemoved(range.first, range.last - 1);
    }
    for (const RowRange &range : changes.inserted) {
        sink_.rowsInserted(range.first, range.last - 1);
    }

    std::array<RowRole, ROW_ROLE_COUNT> scratch{};
    for (const ChangedRow &row : changes.changed) {
        const std::span<const RowRole> roles = rolesFor(row.fields, scratch);
        if (!roles.empty()) {
            sink_.dataChanged(row.row, row.row, roles);
        }
    }

    // Descending, so a move never renumbers a row this loop has yet to visit.
    // The tail order is therefore the reverse of the mask order, which is fine:
    // the store promises an arrival order, not a stable one.
    for (const ChangedRow &changed : std::ranges::reverse_view(changes.changed)) {
        if (carriesOrderingKey(changed.fields)) {
            moveToTail(changed.row);
        }
    }
}

void ResourceListModel::applyLater(ChangeSet changes)
{
    queue_.postChanges(std::move(changes));
    wakeup_.wake();
}

void ResourceListModel::drain()
{
    for (;;) {
        std::optional<Queued> item = queue_.take();
        if (!item) {
            return;
        }
        if (item->kind == Queued::Kind::changes) {
            apply(item->changes);
            continue;
        }
        settlePage(std::move(item->page));
    }
}

void ResourceListModel::onPage(PageOutcome outcome)
{
    queue_.postPage(std::move(outcome));
    // A page produced on the model's thread is drained by the caller; one
    // produced elsewhere needs the hop.
    if (std::this_thread::get_id() == owner_) {
        drain();
        return;
    }
    wakeup_.wake();
}

void ResourceListModel::observe(const domain::k8s::ChangeSet &changes)
{
    queue_.postChanges(changes);
    if (std::this_thread::get_id() == owner_) {
        drain();
        return;
    }
    wakeup_.wake();
}

void ResourceListModel::settlePage(PageOutcome outcome)
{
    fetching_ = false;
    if (!outcome) {
        showRetry(outcome.error());
        return;
    }
    hideRetry();
    continueToken_ = std::move(*outcome);
}

void ResourceListModel::showRetry(PageError error)
{
    logFetchFailure(error);
    if (failure_) {
        // The row is already there: repaint its status rather than churning the
        // view with a remove and an insert.
        const std::uint32_t row = storedRows();
        const std::array<RowRole, 1> roles{RowRole::status};
        sink_.dataChanged(row, row, roles);
        return;
    }
    failure_ = error;
    const std::uint32_t row = storedRows();
    sink_.rowsInserted(row, row);
}

void ResourceListModel::hideRetry()
{
    if (!failure_) {
        return;
    }
    const std::uint32_t row = storedRows();
    failure_.reset();
    sink_.rowsRemoved(row, row);
}

void ResourceListModel::moveToTail(std::uint32_t row)
{
    const std::uint32_t count = rowCount();
    if (row + 1 >= count) {
        return;
    }
    // Moving down, the destination is the index after the new position; the
    // one-past-the-end value is legal for a down move and puts the row last.
    sink_.rowsMoved(row, row, count);
    static_cast<void>(store_.moveToTail(scope_, row));
}

bool ResourceListModel::truncated() const noexcept
{
    const application::k8s::StoreScope *scope = stored();
    return scope != nullptr && scope->state().truncated;
}

std::uint64_t ResourceListModel::refused() const noexcept
{
    const application::k8s::StoreScope *scope = stored();
    return scope == nullptr ? 0 : scope->state().refused;
}

void ResourceListModel::logFetchFailure(PageError error) const
{
    if (logger_ == nullptr) {
        return;
    }
    logger_->write(ports::log::Level::warn, "ui",
                   "page fetch failed for " + scope_.cluster.text() + "/" + scope_.type.resource +
                       ": " + std::string{pageErrorName(error)});
}

} // namespace antares::adapters::ui
