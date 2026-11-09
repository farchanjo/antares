#pragma once

#include "adapters/platform/io/engine_base.hpp"

namespace antares::adapters::platform::io {

/// Portable fallback backend (ADR 0006).
///
/// Registration is implicit: every sweep rebuilds the `pollfd` set from the
/// pending ops, so a ready op costs one entry and an idle engine costs one
/// syscall per timeout. Correct everywhere, outrun by kqueue under load.
class PollEngine final : public EngineBase {
public:
    [[nodiscard]] std::string_view name() const noexcept override;

protected:
    bool waitReady(int timeoutMilliseconds) override;
    void armWakeup() override;
    bool armRead(PendingOp &op) override;
    bool armWrite(PendingOp &op) override;
    bool armAccept(PendingOp &op) override;
    bool armConnect(PendingOp &op) override;
};

} // namespace antares::adapters::platform::io
