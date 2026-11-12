#pragma once

namespace antares::adapters::ui {

/// How a producer on another thread asks the model's own thread to drain
/// (FR-008).
///
/// Under ADR-0020 the model lives on the GUI thread and the store is confined
/// to it, so a batch decoded by a parse worker crosses as a value and the
/// consumer is woken once, after the value is queued. The Qt adapter implements
/// this with `QMetaObject::invokeMethod` and `Qt::QueuedConnection`; a test
/// double calls `drain()` directly, which keeps the suite free of an event loop
/// and of any `sleep_for`.
class ModelWakeup {
public:
    ModelWakeup() = default;
    ModelWakeup(const ModelWakeup &) = delete;
    ModelWakeup &operator=(const ModelWakeup &) = delete;
    ModelWakeup(ModelWakeup &&) = delete;
    ModelWakeup &operator=(ModelWakeup &&) = delete;
    virtual ~ModelWakeup() = default;

    /// Runs on the producing thread. Must not block.
    virtual void wake() = 0;
};

} // namespace antares::adapters::ui
