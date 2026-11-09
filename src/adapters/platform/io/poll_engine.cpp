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

bool PollEngine::armRead(PendingOp &) { return true; }

bool PollEngine::armWrite(PendingOp &) { return true; }

bool PollEngine::armAccept(PendingOp &) { return true; }

bool PollEngine::armConnect(PendingOp &) { return true; }

bool PollEngine::waitReady(int timeoutMilliseconds)
{
    if (ops_.empty()) {
        return false;
    }

    std::vector<struct pollfd> descriptors;
    std::vector<PendingOp *> tracked;
    descriptors.reserve(ops_.size() < MAX_SWEEP ? ops_.size() : MAX_SWEEP);
    tracked.reserve(descriptors.capacity());

    for (auto &owned : ops_) {
        if (descriptors.size() == MAX_SWEEP) {
            break;
        }
        descriptors.push_back({owned->handle->native(), wantedEvents(owned->kind), 0});
        tracked.push_back(owned.get());
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
        const bool failed = (revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;

        switch (op->kind) {
        case OpKind::read: {
            std::expected<Buffer, IoError> result = std::move(op->buffer);
            if (failed) {
                result = std::unexpected(IoError::closed);
            }
            completeRead(*op, std::move(result));
            break;
        }
        case OpKind::write:
            completeWrite(*op, failed ? std::unexpected(IoError::closed)
                                      : std::expected<std::size_t, IoError>{op->buffer.size()});
            break;
        case OpKind::accept: {
            const int accepted = failed ? -1 : ::accept(op->handle->native(), nullptr, nullptr);
            completeAccept(*op, accepted < 0 ? std::unexpected(IoError::system)
                                             : std::expected<int, IoError>{accepted});
            break;
        }
        case OpKind::connect:
            completeConnect(*op, failed ? std::unexpected(IoError::system)
                                        : std::expected<void, IoError>{});
            break;
        }

        release(op);
    }
    return true;
}

} // namespace antares::adapters::platform::io
