#pragma once

#include <cstdint>

#include "adapters/platform/io/engine_base.hpp"

namespace antares::adapters::platform::io {

/// epoll backend. The Linux production engine, the counterpart of kqueue
/// (ADR 0006, ADR 0009, ADR 0018).
///
/// Readiness is registered with `EPOLLONESHOT`, so an interest fires once and
/// the kernel drops it; the op is released after its completion runs. Files
/// never reach this path — the base completes them immediately.
class EpollEngine final : public EngineBase {
public:
    EpollEngine();
    ~EpollEngine() override;

    [[nodiscard]] std::string_view name() const noexcept override;

    /// The set itself: epoll becomes readable while an event is pending.
    [[nodiscard]] int waitHandle() const noexcept override;

    /// False when the kernel refused the set; the factory then falls back.
    [[nodiscard]] bool ready() const noexcept;

protected:
    bool waitReady(int timeoutMilliseconds) override;
    void armWakeup() override;
    bool armRead(PendingOp &op) override;
    bool armWrite(PendingOp &op) override;
    bool armAccept(PendingOp &op) override;
    bool armConnect(PendingOp &op) override;

    void forget(PendingOp &op) override;

private:
    /// Register or drop interest. `events` is the epoll mask; zero removes.
    bool arm(PendingOp& op, std::uint32_t events) const;

    int set_ = -1;
};

} // namespace antares::adapters::platform::io
