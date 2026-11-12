#include "adapters/ui/change_queue.hpp"

#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>

namespace antares::adapters::ui {

void ChangeQueue::postChanges(domain::k8s::ChangeSet changes)
{
    const std::scoped_lock guard{mutex_};
    Queued item;
    item.kind = Queued::Kind::changes;
    item.changes = std::move(changes);
    items_.push_back(std::move(item));
}

void ChangeQueue::postPage(PageOutcome outcome)
{
    const std::scoped_lock guard{mutex_};
    Queued item;
    item.kind = Queued::Kind::page;
    item.page = std::move(outcome);
    items_.push_back(std::move(item));
}

std::optional<Queued> ChangeQueue::take()
{
    const std::scoped_lock guard{mutex_};
    if (items_.empty()) {
        return std::nullopt;
    }
    Queued item = std::move(items_.front());
    items_.pop_front();
    return item;
}

bool ChangeQueue::empty() const
{
    const std::scoped_lock guard{mutex_};
    return items_.empty();
}

std::size_t ChangeQueue::size() const
{
    const std::scoped_lock guard{mutex_};
    return items_.size();
}

} // namespace antares::adapters::ui
