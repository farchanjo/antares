#include "adapters/platform/io/kqueue_engine.hpp"

#include <sys/event.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace antares::adapters::platform::io {
namespace {

constexpr int MAX_EVENTS = 64;

IoError fromEventFlags(const struct kevent &event) noexcept
{
    if ((event.flags & EV_ERROR) != 0) {
        return event.data == EMFILE || event.data == ENFILE ? IoError::tooManyDescriptors
                                                            : IoError::system;
    }
    return IoError::closed;
}

} // namespace

KqueueEngine::KqueueEngine()
    : queue_(::kqueue())
{
}

KqueueEngine::~KqueueEngine()
{
    if (queue_ >= 0) {
        ::close(queue_);
    }
}

std::string_view KqueueEngine::name() const noexcept { return "kqueue"; }

bool KqueueEngine::ready() const noexcept { return queue_ >= 0; }

bool KqueueEngine::arm(PendingOp &op, std::int16_t filter)
{
    if (queue_ < 0) {
        return false;
    }

    struct kevent change {};
    EV_SET(&change, static_cast<uintptr_t>(op.handle->native()), filter, EV_ADD | EV_ONESHOT,
           static_cast<uint32_t>(filter == EVFILT_WRITE), 0, &op);
    return ::kevent(queue_, &change, 1, nullptr, 0, nullptr) == 0;
}

void KqueueEngine::forget(PendingOp &op)
{
    if (queue_ < 0 || op.handle == nullptr) {
        return;
    }

    // Both filters are deleted, because the op may have been armed for either and
    // `EV_ONESHOT` does not say which one is still registered. ENOENT means it
    // already fired or was never armed, which is not a failure.
    const std::uintptr_t descriptor = static_cast<std::uintptr_t>(op.handle->native());
    struct kevent changes[2] {};
    EV_SET(&changes[0], descriptor, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&changes[1], descriptor, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    (void)::kevent(queue_, changes, 2, nullptr, 0, nullptr);
}

void KqueueEngine::armWakeup()
{
    if (queue_ < 0 || wakeReader() < 0) {
        return;
    }
    struct kevent change {};
    EV_SET(&change, static_cast<uintptr_t>(wakeReader()), EVFILT_READ, EV_ADD, 0, 0, nullptr);
    (void)::kevent(queue_, &change, 1, nullptr, 0, nullptr);
}

bool KqueueEngine::armRead(PendingOp &op) { return arm(op, EVFILT_READ); }

bool KqueueEngine::armWrite(PendingOp &op) { return arm(op, EVFILT_WRITE); }

bool KqueueEngine::armAccept(PendingOp &op) { return arm(op, EVFILT_READ); }

bool KqueueEngine::armConnect(PendingOp &op) { return arm(op, EVFILT_WRITE); }

bool KqueueEngine::waitReady(int timeoutMilliseconds)
{
    struct timespec timeout {
        timeoutMilliseconds / 1000, static_cast<long>((timeoutMilliseconds % 1000)) * 1000000L
    };

    struct kevent events[MAX_EVENTS];
    const int ready = ::kevent(queue_, nullptr, 0, events, MAX_EVENTS,
                               timeoutMilliseconds < 0 ? nullptr : &timeout);
    if (ready <= 0) {
        return false;
    }

    for (int index = 0; index < ready; ++index) {
        const struct kevent &event = events[index];
        auto *op = static_cast<PendingOp *>(event.udata);
        if (op == nullptr) {
            continue;
        }

        (void)event;
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
    return true;
}

} // namespace antares::adapters::platform::io
