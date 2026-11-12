#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "adapters/platform/io/engine_base.hpp"

struct io_uring;
struct io_uring_sqe;

namespace antares::adapters::platform::io {

/// io_uring backend. The preferred Linux engine, ahead of epoll (ADR 0018).
///
/// Readiness, not completion: a poll request fires, then the base performs the
/// syscall. That keeps the semantics identical to kqueue and epoll, so a bug
/// here cannot disguise itself as a different bug there.
///
/// A poll fires once, unlike the persistent interest the other two hold. Two
/// consequences shape this file: exactly one request is parked on the wake pipe
/// and rearmed as it fires, and the loop never sleeps longer than a short cap,
/// so a lost request costs latency instead of a hang.
class IoUringEngine final : public EngineBase {
public:
    IoUringEngine();
    ~IoUringEngine() override;

    [[nodiscard]] std::string_view name() const noexcept override;

    /// The ring descriptor: a pending completion makes it readable.

    /// False when the kernel refused the ring; the factory then falls back.
    [[nodiscard]] bool ready() const noexcept;

    /// The ring descriptor, readable while a completion is pending.
    [[nodiscard]] int waitHandle() const noexcept override;

protected:
    bool waitReady(int timeoutMilliseconds) override;
    void armWakeup() override;
    bool armRead(PendingOp &op) override;
    bool armWrite(PendingOp &op) override;
    bool armAccept(PendingOp &op) override;
    bool armConnect(PendingOp &op) override;

    /// Withdraw a queued poll request, so its completion cannot land on a freed
    /// op after the base releases it.
    void forget(PendingOp &op) override;

private:
    /// Queue one poll request for `op`. False when no slot can be had.
    bool arm(PendingOp &op, std::uint32_t mask);

    /// Park a request on the wake pipe unless one is already parked.
    /// Assumes `ringMutex_` is held.
    void rearmWake();

    /// Rearm and flush after a hosted turn. A poll request fires once, so
    /// without this the host's next sleep has nothing armed to wake it.
    void turnEnded() override;

    /// Submit everything queued, looping over a short submit. Assumes the lock.
    bool flushLocked();

    /// A free submission slot, flushing when the ring is full. Assumes the lock.
    [[nodiscard]] struct io_uring_sqe *nextSlot();

    io_uring *ring_ = nullptr;

    /// Captured once: the ring is freed before the descriptor is used again, and
    /// `ring_->ring_fd` would need the definition in this header.
    int ringFd_ = -1;

    /// kqueue and epoll arm through a syscall, which is thread-safe on its own.
    /// The ring is memory both threads touch, so every SQ access takes this.
    std::mutex ringMutex_;

    /// Requests queued but not yet accepted by the kernel.
    std::uint32_t queued_ = 0;

    /// True while a poll request sits on the wake pipe. Without it each pass
    /// would park another, filling the ring with dead requests.
    bool wakeArmed_ = false;

    /// Live requests by tag. A cancelled op is erased here before it is
    /// released, so its completion — which may already sit in the ring — finds
    /// no entry and is skipped rather than read through a freed pointer.
    std::unordered_map<std::uint64_t, PendingOp *> byTag_;

    /// Source of request tags. The two fixed tags are 1 and 2, so this starts
    /// clear of both.
    std::uint64_t lastTag_ = 16;
};

} // namespace antares::adapters::platform::io
