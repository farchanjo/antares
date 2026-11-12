#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

#include "adapters/ui/queued.hpp"
#include "domain/k8s/change_set.hpp"

namespace antares::adapters::ui {

/// The crossing from a producing thread to the model's own (FR-008, P-12).
///
/// A `ChangeSet` is a value, so the queue takes it by value and never a
/// reference into the producer's batch. One mutex guards the deque; the lock is
/// held for a push and a pop, never across a model notification, so a producer
/// never waits on a repaint.
///
/// The queue is not the model's row storage: it holds pending batches, and a
/// drained batch frees its ranges. That is what keeps the model footprint flat.
class ChangeQueue {
public:
    ChangeQueue() = default;
    ChangeQueue(const ChangeQueue &) = delete;
    ChangeQueue &operator=(const ChangeQueue &) = delete;
    ChangeQueue(ChangeQueue &&) = delete;
    ChangeQueue &operator=(ChangeQueue &&) = delete;
    ~ChangeQueue() = default;

    void postChanges(domain::k8s::ChangeSet changes);
    void postPage(PageOutcome outcome);

    /// The oldest item, or nothing when the queue is empty.
    [[nodiscard]] std::optional<Queued> take();

    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::deque<Queued> items_;
};

} // namespace antares::adapters::ui
