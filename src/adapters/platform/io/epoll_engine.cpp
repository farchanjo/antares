#include "adapters/platform/io/epoll_engine.hpp"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace antares::adapters::platform::io {
namespace {

constexpr int MAX_EVENTS = 64;

} // namespace

EpollEngine::EpollEngine()
    : set_(::epoll_create1(EPOLL_CLOEXEC))
{
}

EpollEngine::~EpollEngine()
{
    if (set_ >= 0) {
        ::close(set_);
    }
}

std::string_view EpollEngine::name() const noexcept { return "epoll"; }

bool EpollEngine::ready() const noexcept { return set_ >= 0; }

int EpollEngine::waitHandle() const noexcept { return set_; }

bool EpollEngine::arm(PendingOp& op, std::uint32_t events) const {
    if (set_ < 0 || op.handle == nullptr) {
        return false;
    }

    struct epoll_event change {};
    // The op address is stable for its life and doubled as the kqueue udata;
    // here it rides in data.ptr and comes back on the event.
    change.events = events | EPOLLONESHOT;
    change.data.ptr = &op;

    // MOD first: a re-armed descriptor is already in the set, and ADD would
    // fail with EEXIST and silently lose the interest.
    if (::epoll_ctl(set_, EPOLL_CTL_MOD, op.handle->native(), &change) == 0) {
        return true;
    }
    return ::epoll_ctl(set_, EPOLL_CTL_ADD, op.handle->native(), &change) == 0;
}

void EpollEngine::armWakeup()
{
    if (set_ < 0 || wakeReader() < 0) {
        return;
    }

    struct epoll_event change {};
    // A null op marks the wake pipe: nothing to complete, only to drain.
    change.events = EPOLLIN;
    change.data.ptr = nullptr;
    (void)::epoll_ctl(set_, EPOLL_CTL_ADD, wakeReader(), &change);
}

bool EpollEngine::armRead(PendingOp &op) { return arm(op, EPOLLIN); }

bool EpollEngine::armWrite(PendingOp &op) { return arm(op, EPOLLOUT); }

bool EpollEngine::armAccept(PendingOp &op) { return arm(op, EPOLLIN); }

bool EpollEngine::armConnect(PendingOp &op) { return arm(op, EPOLLOUT); }

void EpollEngine::forget(PendingOp &op)
{
    if (set_ < 0 || op.handle == nullptr) {
        return;
    }
    // DEL drops every mask on the descriptor, which is what a cancel wants: no
    // interest may outlive the op it pointed at.
    (void)::epoll_ctl(set_, EPOLL_CTL_DEL, op.handle->native(), nullptr);
}

bool EpollEngine::waitReady(int timeoutMilliseconds)
{
    struct epoll_event events[MAX_EVENTS];
    const int ready =
        ::epoll_wait(set_, events, MAX_EVENTS, timeoutMilliseconds < 0 ? -1 : timeoutMilliseconds);
    if (ready <= 0) {
        return false;
    }

    for (int index = 0; index < ready; ++index) {
        const struct epoll_event &event = events[index];
        auto *op = static_cast<PendingOp *>(event.data.ptr);
        if (op == nullptr) {
            // The wake pipe. The loop drains it at the top of every pass, so
            // there is nothing to do beyond skipping it here.
            continue;
        }

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
