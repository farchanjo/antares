#pragma once

#include "adapters/platform/io/engine_base.hpp"

namespace antares::adapters::platform::io {

/// kqueue backend. The macOS production engine (ADR 0006, ADR 0009).
///
/// Readiness is registered with `EV_ONESHOT`, so a filter fires once and the
/// kernel drops it; the op is released after its completion runs. Files never
/// reach this path — the base completes them immediately.
class KqueueEngine final : public EngineBase {
public:
    KqueueEngine();
    ~KqueueEngine() override;

    [[nodiscard]] std::string_view name() const noexcept override;

    /// False when the kernel refused the queue; the factory then falls back.
    [[nodiscard]] bool ready() const noexcept;

protected:
    bool waitReady(int timeoutMilliseconds) override;
    void armWakeup() override;
    bool armRead(PendingOp &op) override;
    bool armWrite(PendingOp &op) override;
    bool armAccept(PendingOp &op) override;
    bool armConnect(PendingOp &op) override;

    /// Drops the registration an `EV_ONESHOT` left behind. Without it a cancelled
    /// op stays armed with `udata` pointing at freed memory, and the next byte on
    /// that descriptor reaches it (FR-001).
    void forget(PendingOp &op) override;

private:
    bool arm(PendingOp &op, std::int16_t filter);

    int queue_ = -1;
};

} // namespace antares::adapters::platform::io
