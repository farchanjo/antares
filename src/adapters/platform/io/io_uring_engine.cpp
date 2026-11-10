#include "adapters/platform/io/io_uring_engine.hpp"

#include <cerrno>
#include <poll.h>

#include <liburing.h>

namespace antares::adapters::platform::io {
namespace {

constexpr unsigned RING_ENTRIES = 256;
constexpr int MAX_BATCH = 64;

/// Longest the loop may sleep. The other two backends hold a persistent
/// interest and can block forever; a poll request fires once, so a lost one
/// would hang the engine. Capping the sleep makes the parked request an
/// optimisation rather than a correctness requirement.
constexpr int IDLE_TIMEOUT_MS = 200;

/// Completion tags. A real op is tagged with its own address, so the two fixed
/// values only have to stay clear of heap pointers — 1 and 2 always are.
///
/// The split matters: a `poll_remove` acknowledgement also carries a tag, and
/// treating it as the wake pipe would clear the flag with the request still
/// parked, which loses the wakeup for good.
constexpr std::uint64_t wakeTag = 1;
constexpr std::uint64_t auxTag = 2;

/// What a poll request asks for, from the op kind. Mirrors the epoll masks.
[[nodiscard]] std::uint32_t maskFor(OpKind kind) noexcept
{
    return kind == OpKind::write || kind == OpKind::connect ? POLLOUT : POLLIN;
}

} // namespace

IoUringEngine::IoUringEngine()
    : ring_(new io_uring{})
{
    if (io_uring_queue_init(RING_ENTRIES, ring_, 0) < 0) {
        delete ring_;
        ring_ = nullptr;
    }
}

IoUringEngine::~IoUringEngine()
{
    if (ring_ != nullptr) {
        io_uring_queue_exit(ring_);
        delete ring_;
    }
}

std::string_view IoUringEngine::name() const noexcept { return "io_uring"; }

bool IoUringEngine::ready() const noexcept { return ring_ != nullptr; }

bool IoUringEngine::flushLocked()
{
    if (ring_ == nullptr) {
        return false;
    }

    // A short submit is normal: the kernel takes what it has room for. Loop,
    // because the caller is about to block and a stranded request would never
    // reach the kernel to fire.
    while (queued_ > 0) {
        const int submitted = io_uring_submit(ring_);
        if (submitted <= 0) {
            return submitted == 0;
        }
        queued_ -= static_cast<std::uint32_t>(submitted);
    }
    return true;
}

struct io_uring_sqe *IoUringEngine::nextSlot()
{
    if (ring_ == nullptr) {
        return nullptr;
    }
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
    if (sqe != nullptr) {
        return sqe;
    }
    (void)flushLocked();
    return io_uring_get_sqe(ring_);
}

bool IoUringEngine::arm(PendingOp &op, std::uint32_t mask)
{
    const std::lock_guard<std::mutex> guard{ringMutex_};
    if (ring_ == nullptr || op.handle == nullptr) {
        return false;
    }

    struct io_uring_sqe *sqe = nextSlot();
    if (sqe == nullptr) {
        return false;
    }

    // The op address is the tag. It is stable for the op's life, and it is the
    // same value the removal path hands back.
    io_uring_prep_poll_add(sqe, op.handle->native(), mask);
    sqe->user_data = reinterpret_cast<std::uint64_t>(&op);
    ++queued_;
    return true;
}

void IoUringEngine::rearmWake()
{
    if (ring_ == nullptr || wakeReader() < 0 || wakeArmed_) {
        return;
    }

    struct io_uring_sqe *sqe = nextSlot();
    if (sqe == nullptr) {
        return;
    }

    // A wake tag marks the pipe: nothing to complete, only to drain.
    io_uring_prep_poll_add(sqe, wakeReader(), POLLIN);
    sqe->user_data = wakeTag;
    wakeArmed_ = true;
    ++queued_;
}

void IoUringEngine::armWakeup()
{
    const std::lock_guard<std::mutex> guard{ringMutex_};
    rearmWake();
    (void)flushLocked();
}

bool IoUringEngine::armRead(PendingOp &op) { return arm(op, maskFor(OpKind::read)); }

bool IoUringEngine::armWrite(PendingOp &op) { return arm(op, maskFor(OpKind::write)); }

bool IoUringEngine::armAccept(PendingOp &op) { return arm(op, maskFor(OpKind::accept)); }

bool IoUringEngine::armConnect(PendingOp &op) { return arm(op, maskFor(OpKind::connect)); }

void IoUringEngine::forget(PendingOp &op)
{
    const std::lock_guard<std::mutex> guard{ringMutex_};
    struct io_uring_sqe *sqe = nextSlot();
    if (sqe == nullptr) {
        return;
    }

    // Removing a request that already completed is harmless: the kernel answers
    // ENOENT, which arrives as an auxiliary completion and is ignored.
    io_uring_prep_poll_remove(sqe, reinterpret_cast<std::uint64_t>(&op));
    sqe->user_data = auxTag;
    ++queued_;
    (void)flushLocked();
}

bool IoUringEngine::waitReady(int timeoutMilliseconds)
{
    if (ring_ == nullptr) {
        return false;
    }

    {
        const std::lock_guard<std::mutex> guard{ringMutex_};
        rearmWake();
        (void)flushLocked();
    }
    if (ring_ == nullptr) {
        return false;
    }

    // A zero timespec to `io_uring_enter` means "wait forever", not "do not
    // wait", so a pending task would stall the loop. Zero peeks instead.
    struct io_uring_cqe *first = nullptr;
    if (timeoutMilliseconds == 0) {
        if (io_uring_peek_cqe(ring_, &first) < 0) {
            return false;
        }
    } else {
        const int budget = timeoutMilliseconds < 0 ? IDLE_TIMEOUT_MS : timeoutMilliseconds;
        struct __kernel_timespec timeout {};
        timeout.tv_sec = budget / 1000;
        timeout.tv_nsec = static_cast<long long>(budget % 1000) * 1000000LL;

        if (io_uring_wait_cqe_timeout(ring_, &first, &timeout) < 0) {
            return false;
        }
    }

    int handled = 0;
    struct io_uring_cqe *cqe = first;
    while (cqe != nullptr) {
        if (cqe->user_data == auxTag) {
            // A removal acknowledged. Nothing to complete.
            io_uring_cqe_seen(ring_, cqe);
            cqe = nullptr;
            if (io_uring_peek_cqe(ring_, &cqe) < 0) {
                cqe = nullptr;
            }
            continue;
        }
        if (cqe->user_data == wakeTag) {
            wakeArmed_ = false;
            io_uring_cqe_seen(ring_, cqe);
            cqe = nullptr;
            if (io_uring_peek_cqe(ring_, &cqe) < 0) {
                cqe = nullptr;
            }
            continue;
        }

        // A cancelled request still reports. `cancelPending` already completed
        // the op and dropped it, so acting again would run the callback twice.
        if (cqe->res == -ECANCELED) {
            io_uring_cqe_seen(ring_, cqe);
            cqe = nullptr;
            if (io_uring_peek_cqe(ring_, &cqe) < 0) {
                cqe = nullptr;
            }
            continue;
        }

        auto *op = reinterpret_cast<PendingOp *>(cqe->user_data);
        if (op != nullptr) {
            switch (op->kind) {
            case OpKind::read:
                performRead(*op);
                break;
            case OpKind::write:
                performWrite(*op);
                break;
            case OpKind::accept:
                performAccept(*op);
                break;
            case OpKind::connect:
                performConnect(*op);
                break;
            }
            release(op);
        }

        io_uring_cqe_seen(ring_, cqe);
        if (++handled >= MAX_BATCH) {
            // Leave the rest for the next pass, so one busy descriptor cannot
            // starve the posted tasks and the timers.
            break;
        }
        cqe = nullptr;
        if (io_uring_peek_cqe(ring_, &cqe) < 0) {
            cqe = nullptr;
        }
    }
    return true;
}

} // namespace antares::adapters::platform::io
