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
    const int ready = ::kevent(queue_, nullptr, 0, events, MAX_EVENTS, &timeout);
    if (ready <= 0) {
        return false;
    }

    for (int index = 0; index < ready; ++index) {
        const struct kevent &event = events[index];
        auto *op = static_cast<PendingOp *>(event.udata);
        if (op == nullptr) {
            continue;
        }

        const bool failed = (event.flags & EV_ERROR) != 0 || (event.flags & EV_EOF) != 0;
        switch (op->kind) {
        case OpKind::read: {
            std::expected<Buffer, IoError> result = std::move(op->buffer);
            if (failed) {
                result = std::unexpected(fromEventFlags(event));
            }
            completeRead(*op, std::move(result));
            break;
        }
        case OpKind::write:
            completeWrite(*op, failed ? std::unexpected(fromEventFlags(event))
                                      : std::expected<std::size_t, IoError>{op->buffer.size()});
            break;
        case OpKind::accept: {
            const int accepted = failed ? -1 : ::accept(op->handle->native(), nullptr, nullptr);
            completeAccept(*op, accepted < 0 ? std::unexpected(fromEventFlags(event))
                                             : std::expected<int, IoError>{accepted});
            break;
        }
        case OpKind::connect:
            completeConnect(*op, failed ? std::unexpected(fromEventFlags(event))
                                        : std::expected<void, IoError>{});
            break;
        }

        release(op);
    }
    return true;
}

} // namespace antares::adapters::platform::io
