#include "adapters/platform/io/poll_engine.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <vector>

namespace antares::adapters::platform::io {
namespace {

constexpr std::size_t MAX_SWEEP = 1024;

short wantedEvents(OpKind kind) noexcept
{
    return kind == OpKind::write || kind == OpKind::connect ? POLLOUT : POLLIN;
}

} // namespace

std::string_view PollEngine::name() const noexcept { return "poll"; }

void PollEngine::armWakeup() {}

bool PollEngine::armRead(PendingOp &) { return true; }

bool PollEngine::armWrite(PendingOp &) { return true; }

bool PollEngine::armAccept(PendingOp &) { return true; }

bool PollEngine::armConnect(PendingOp &) { return true; }

bool PollEngine::waitReady(int timeoutMilliseconds)
{
    if (ops_.empty() && wakeReader() < 0) {
        return false;
    }

    std::vector<struct pollfd> descriptors;
    std::vector<PendingOp *> tracked;

    // Index 0 is always the wakeup pipe: an idle engine still blocks in poll
    // instead of spinning on an empty descriptor set.
    descriptors.push_back({wakeReader(), POLLIN, 0});
    tracked.push_back(nullptr);

    descriptors.reserve(descriptors.size() + (ops_.size() < MAX_SWEEP ? ops_.size() : MAX_SWEEP));
    tracked.reserve(descriptors.capacity());

    for (auto &owned : ops_) {
        if (descriptors.size() == MAX_SWEEP + 1) {
            break;
        }
        if (!owned->readiness) {
            continue;
        }
        descriptors.push_back({owned->handle->native(), wantedEvents(owned->kind), 0});
        tracked.push_back(owned.get());
    }

    if (descriptors.empty()) {
        return false;
    }

    const int ready = ::poll(descriptors.data(), descriptors.size(), timeoutMilliseconds);
    if (ready <= 0) {
        return false;
    }

    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        const short revents = descriptors[index].revents;
        if (revents == 0) {
            continue;
        }

        PendingOp *op = tracked[index];
        if (op == nullptr) {
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
