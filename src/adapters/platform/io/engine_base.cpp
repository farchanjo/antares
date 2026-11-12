#include "adapters/platform/io/engine_base.hpp"

#include <algorithm>
#include <cstdio>

#include "domain/units/instant.hpp"

#if defined(__APPLE__)
#include "platform/mac/thread_tuning.hpp"
namespace tuning = antares::platform::mac;
#else
#include "platform/linux/thread_tuning.hpp"
namespace tuning = antares::platform::linux;
#endif

#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <sys/socket.h>

#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>

#include <expected>

namespace antares::adapters::platform::io {
namespace {

constexpr int POLL_TIMEOUT_MS = 50;
constexpr std::size_t READ_CHUNK = std::size_t{64} * 1024;

/// How long one turn keeps draining before it yields (FR-009). A burst of five
/// thousand tasks is 60 ms today; without a bound it would all run in one turn.
constexpr std::chrono::milliseconds TURN_BUDGET{4};

} // namespace

EngineBase::EngineBase()
{
    if (::pipe(wake_) == 0) {
        ::fcntl(wake_[0], F_SETFL, O_NONBLOCK);
        ::fcntl(wake_[1], F_SETFL, O_NONBLOCK);
    }
}

EngineBase::~EngineBase()
{
    stop();

    // Whatever was posted after the last drain is still on the stack; its nodes
    // are ours to free, and dropping them runs no task.
    for (TaskNode *node = head_.exchange(nullptr); node != nullptr;) {
        const std::unique_ptr<TaskNode> owned{node};
        node = owned->next;
    }
    if (wake_[0] >= 0) {
        ::close(wake_[0]);
    }
    if (wake_[1] >= 0) {
        ::close(wake_[1]);
    }
}

int EngineBase::wakeReader() const noexcept { return wake_[0]; }

std::size_t EngineBase::pendingCount() const noexcept
{
    const std::lock_guard<std::recursive_mutex> guard{opsMutex_};
    return ops_.size();
}

std::unique_ptr<PendingOp> EngineBase::allocate(OpKind kind, const IoHandle &handle,
                                                bool readiness)
{
    auto op = std::make_unique<PendingOp>();
    op->kind = kind;
    op->handle = &handle;
    // Published already decided: the loop may pick the op up the instant it is
    // in the store, so a field written after the push is a field read stale.
    op->readiness = readiness;
    return op;
}

PendingOp *EngineBase::publish(std::unique_ptr<PendingOp> op)
{
    const std::lock_guard<std::recursive_mutex> guard{opsMutex_};
    ops_.push_back(std::move(op));
    ops_.back()->slot = std::prev(ops_.end());
    return ops_.back().get();
}

std::expected<void, IoError> EngineBase::submitRead(const IoHandle &handle,
                                                     std::optional<Bytes> offset, Bytes maxBytes,
                                                     ReadCompletion done)
{
    submitted_.fetch_add(1, std::memory_order_relaxed);
    if (!handle.valid() || maxBytes.isZero()) {
        return std::unexpected(IoError::system);
    }

    const std::lock_guard<std::recursive_mutex> guard{opsMutex_};
    auto owned = allocate(OpKind::read, handle, !handle.isFile());
    owned->offset = offset.value_or(Bytes{});
    owned->buffer.resize(maxBytes.count());
    owned->onRead = std::move(done);
    PendingOp *op = publish(std::move(owned));

    if (!op->readiness) {
        completeImmediately(*op);
        return {};
    }

    if (!armRead(*op)) {
        op->onRead = nullptr;
        releaseLast();
        return std::unexpected(IoError::tooManyDescriptors);
    }
    poke();
    return {};
}

std::expected<void, IoError> EngineBase::submitWrite(const IoHandle &handle,
                                                      std::optional<Bytes> offset, Buffer bytes,
                                                      WriteCompletion done)
{
    submitted_.fetch_add(1, std::memory_order_relaxed);
    if (!handle.valid() || bytes.empty()) {
        return std::unexpected(IoError::system);
    }

    auto owned = allocate(OpKind::write, handle, !handle.isFile());
    owned->offset = offset.value_or(Bytes{});
    owned->buffer = std::move(bytes);
    owned->onWrite = std::move(done);
    PendingOp *op = publish(std::move(owned));

    if (!op->readiness) {
        completeImmediately(*op);
        return {};
    }

    if (!armWrite(*op)) {
        op->onWrite = nullptr;
        releaseLast();
        return std::unexpected(IoError::tooManyDescriptors);
    }
    poke();
    return {};
}

std::expected<void, IoError> EngineBase::submitAccept(const IoHandle &listener,
                                                       AcceptCompletion done)
{
    submitted_.fetch_add(1, std::memory_order_relaxed);
    if (!listener.valid()) {
        return std::unexpected(IoError::system);
    }

    auto owned = allocate(OpKind::accept, listener, true);
    owned->onAccept = std::move(done);
    PendingOp *op = publish(std::move(owned));

    if (!armAccept(*op)) {
        op->onAccept = nullptr;
        releaseLast();
        return std::unexpected(IoError::tooManyDescriptors);
    }
    poke();
    return {};
}

std::expected<void, IoError> EngineBase::submitConnect(const IoHandle &handle,
                                                        std::span<const std::byte> address,
                                                        ConnectCompletion done)
{
    submitted_.fetch_add(1, std::memory_order_relaxed);
    if (!handle.valid() || address.empty()) {
        return std::unexpected(IoError::system);
    }

    // Actually connect: the filter alone only reports writability, which an
    // unconnected socket already has.
    const int descriptor = handle.native();
    const int flags = ::fcntl(descriptor, F_GETFL, 0);
    (void)::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK);

    auto owned = allocate(OpKind::connect, handle, true);
    owned->onConnect = std::move(done);
    PendingOp *op = publish(std::move(owned));

    if (::connect(descriptor, reinterpret_cast<const sockaddr *>(address.data()), address.size()) ==
        0) {
        op->readiness = false;
        completeImmediately(*op);
        return {};
    }
    if (errno != EINPROGRESS) {
        op->readiness = false;
        post([this, op] {
            completeConnect(*op, std::unexpected(IoError::system));
            release(op);
        });
        return {};
    }

    if (!armConnect(*op)) {
        op->onConnect = nullptr;
        releaseLast();
        return std::unexpected(IoError::tooManyDescriptors);
    }
    poke();
    return {};
}

EngineStats EngineBase::stats() const noexcept
{
    EngineStats snapshot;
    snapshot.submitted = submitted_.load(std::memory_order_relaxed);
    snapshot.completed = completed_.load(std::memory_order_relaxed);
    for (std::size_t index = 0; index < FAILURE_FAMILIES; ++index) {
        snapshot.failed[index] = failed_[index].load(std::memory_order_relaxed);
    }
    return snapshot;
}

bool EngineBase::isEngineThread() const noexcept
{
    return std::this_thread::get_id() == loopThread_.load(std::memory_order_relaxed);
}

void EngineBase::poke() noexcept
{
    // Only the wake byte. Stopping and joining here would end the resolver on
    // the first wakeup and race the join against a detach from the resolver
    // thread itself; `stop()` owns that.
    if (wake_[1] >= 0) {
        const char byte = 1;
        (void)::write(wake_[1], &byte, 1);
    }
}

std::expected<void, IoError> EngineBase::submitTimer(domain::Duration delay, PostedTask task)
{
    if (!task) {
        return std::unexpected(IoError::system);
    }

    {
        const std::lock_guard<std::mutex> guard{timerMutex_};
        timers_.push_back(Timer{domain::afterFromNow(delay), std::move(task)});
    }
    poke();
    return {};
}

std::expected<void, IoError> EngineBase::submitResolve(std::string host, std::uint16_t port,
                                                       ResolveDone done)
{
    if (!done || host.empty()) {
        return std::unexpected(IoError::system);
    }

    const auto serial = resolveSerial_.fetch_add(1) + 1;
    auto shared = std::make_shared<ResolveDone>(std::move(done));
    auto claim = std::make_shared<ResolveClaim>();

    {
        const std::lock_guard<std::mutex> guard{resolveMutex_};
        resolveQueue_.push_back(ResolveJob{serial, std::move(host), port, shared, claim});
    }
    resolveReady_.notify_one();
    startResolver();

    // The watchdog. It runs on the loop, which outlives every timer, so the
    // bare `this` is sound; `claimResolve` makes sure only one of the two
    // settles the job.
    static_cast<void>(submitTimer(resolveTimeout, [claim, shared] {
        if (!claimResolve(claim)) {
            return;
        }
        (*shared)(std::unexpected(IoError::timeout));
    }));
    return {};
}

bool EngineBase::claimResolve(const std::shared_ptr<ResolveClaim> &claim)
{
    return claim != nullptr && !claim->settled.exchange(true, std::memory_order_acq_rel);
}

void EngineBase::startResolver()
{
    const std::lock_guard<std::mutex> guard{resolveMutex_};
    if (resolver_.joinable()) {
        return;
    }
    resolver_ = std::thread([this] { resolverLoop(); });
}

void EngineBase::resolverLoop()
{
    // One blocking `getaddrinfo` at a time, never on the loop.
    while (true) {
        ResolveJob job;
        {
            std::unique_lock<std::mutex> lock{resolveMutex_};
            resolveReady_.wait(lock, [this] { return resolveStop_ || !resolveQueue_.empty(); });
            if (resolveStop_ && resolveQueue_.empty()) {
                return;
            }
            job = std::move(resolveQueue_.front());
            resolveQueue_.pop_front();
        }

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo *found = nullptr;
        const std::string service = std::to_string(job.port);
        std::fprintf(stderr, "  resolve %s:%s\n", job.host.c_str(), service.c_str());
        ResolveResult result = std::unexpected(IoError::system);

        if (::getaddrinfo(job.host.c_str(), service.c_str(), &hints, &found) == 0 &&
            found != nullptr) {
            // Prefer IPv4: dual-stack hosts commonly answer `::1` first while
            // the service listens on 127.0.0.1 only.
            const addrinfo *chosen = found;
            for (const addrinfo *candidate = found; candidate != nullptr;
                 candidate = candidate->ai_next) {
                if (candidate->ai_family == AF_INET) {
                    chosen = candidate;
                    break;
                }
            }

            sockaddr_storage storage{};
            std::memcpy(&storage, chosen->ai_addr, chosen->ai_addrlen);
            ::freeaddrinfo(found);
            result = storage;
        }

        std::fprintf(stderr, "  resolved ok=%d\n", static_cast<int>(result.has_value()));
        post([this, job, result = std::move(result)]() mutable {
            if (!claimResolve(job.claim)) {
                return;
            }
            (*job.done)(std::move(result));
        });
    }
}

void EngineBase::forget(PendingOp &op) { (void)op; }

void EngineBase::cancelPending(const IoHandle &handle)
{
    // Completed on the loop, like any other op, so a callback never runs from
    // the caller's thread.
    post([this, descriptor = handle.native()] {
        std::vector<PendingOp *> doomed;
        {
            // The walk takes the lock; the completions below do not, because a
            // completion re-enters `release` and would deadlock on a held lock.
            const std::lock_guard<std::recursive_mutex> guard{opsMutex_};
            for (const auto &op : ops_) {
                if (op->handle != nullptr && op->handle->native() == descriptor) {
                    doomed.push_back(op.get());
                }
            }
        }

        for (PendingOp *op : doomed) {
            completeCancelled(*op);
        }
    });
}

void EngineBase::completeCancelled(PendingOp &op)
{
    switch (op.kind) {
    case OpKind::read:
        completeRead(op, std::unexpected(IoError::cancelled));
        break;
    case OpKind::write:
        completeWrite(op, std::unexpected(IoError::cancelled));
        break;
    case OpKind::connect:
        completeConnect(op, std::unexpected(IoError::cancelled));
        break;
    case OpKind::accept:
        completeAccept(op, std::unexpected(IoError::cancelled));
        break;
    }
    forget(op);
    release(&op);
}

void EngineBase::pushTask(PostedTask task)
{
    auto node = std::make_unique<TaskNode>(TaskNode{std::move(task), nullptr});
    node->next = head_.load(std::memory_order_relaxed);
    // Release publishes the node contents; a failure means another producer won
    // and the expected value was refreshed for us.
    while (!head_.compare_exchange_weak(node->next, node.get(), std::memory_order_release,
                                        std::memory_order_relaxed)) {
    }
    (void)node.release();
}

EngineBase::TaskNode *EngineBase::takeTasks() noexcept
{
    TaskNode *stack = head_.exchange(nullptr, std::memory_order_acquire);
    TaskNode *ordered = nullptr;
    while (stack != nullptr) {
        TaskNode *next = stack->next;
        stack->next = ordered;
        ordered = stack;
        stack = next;
    }
    return ordered;
}

void EngineBase::post(PostedTask task)
{
    pushTask(std::move(task));
    poke();
}

void EngineBase::cancelAll()
{
    std::vector<PendingOp *> doomed;
    {
        const std::lock_guard<std::recursive_mutex> guard{opsMutex_};
        doomed.reserve(ops_.size());
        for (const auto &op : ops_) {
            doomed.push_back(op.get());
        }
    }
    for (PendingOp *op : doomed) {
        completeCancelled(*op);
    }
}

void EngineBase::run()
{
    // The first run must keep a stop() issued before the thread got here, or the
    // loop runs forever; a later run clears it (FR-006).
    if (loopStarted_.exchange(true)) {
        stopped_.store(false);
    }
    tuning::tuneLoopThread("antares.io");
    loopThread_.store(std::this_thread::get_id(), std::memory_order_relaxed);
    loopRunning_.store(true, std::memory_order_release);
    armWakeup();
    while (!stopped_.load()) {
        drainWakeup();
        drainUntil(std::chrono::steady_clock::now() + TURN_BUDGET);
        fireDueTimers();
        if (stopped_.load()) {
            break;
        }
        (void)waitReady(nextWaitMilliseconds());
    }

    // Whatever was queued behind the stop still has to run: `stop()` posts the
    // cancellation precisely so it lands here, on the loop.
    drainUntil(std::chrono::steady_clock::time_point::max());
    loopRunning_.store(false, std::memory_order_release);
}

void EngineBase::stop()
{
    // Completion callbacks run on the loop only, so the cancellation is posted
    // and the loop drains it on the way out. With no loop running it runs here,
    // where nothing else can be touching the store.
    if (stopped_.exchange(true)) {
        return;
    }
    post([this] { cancelAll(); });
    {
        const std::lock_guard<std::mutex> guard{resolveMutex_};
        resolveStop_ = true;
    }
    resolveReady_.notify_all();
    if (resolver_.joinable()) {
        // `stop()` may run on the resolver itself when a completion unwinds
        // into the owner, and a thread cannot join itself.
        if (resolver_.get_id() == std::this_thread::get_id()) {
            resolver_.detach();
        } else {
            resolver_.join();
        }
    }

    if (wake_[1] >= 0) {
        const char byte = 1;
        (void)::write(wake_[1], &byte, 1);
    }

    // No loop to drain it: settle the queue here, on the caller.
    if (!loopRunning_.load(std::memory_order_acquire)) {
        drainUntil(std::chrono::steady_clock::time_point::max());
        return;
    }

    // A loop is running and will drain on its way out. Waiting for it is what
    // makes `stop()` a boundary: afterwards nothing the loop owns can still run,
    // so a task posted later is known to be orphaned rather than racing.
    if (isEngineThread()) {
        return;
    }
    const auto limit = std::chrono::steady_clock::now() + stopWait;
    while (loopRunning_.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < limit) {
        std::this_thread::yield();
    }
}

void EngineBase::fireDueTimers()
{
    const std::lock_guard<std::mutex> guard{timerMutex_};
    if (timers_.empty()) {
        return;
    }

    const domain::Instant now = std::chrono::steady_clock::now();
    std::vector<Timer> pending;
    pending.reserve(timers_.size());

    for (Timer &timer : timers_) {
        if (timer.due <= now) {
            pushTask(std::move(timer.task));
            continue;
        }
        pending.push_back(std::move(timer));
    }
    timers_.swap(pending);
}

int EngineBase::nextWaitMilliseconds() const
{
    // A queued task must run before any wait, or it sits until the next event.
    if (head_.load(std::memory_order_acquire) != nullptr) {
        return 0;
    }

    const std::lock_guard<std::mutex> guard{timerMutex_};

    // Nothing pending: block until the wakeup pipe says otherwise. A fixed
    // timeout here would cost an idle wakeup per period for no work.
    if (timers_.empty() && pendingCount() == 0) {
        return -1;
    }
    if (timers_.empty()) {
        return POLL_TIMEOUT_MS;
    }

    const auto nearest = std::min_element(
        timers_.begin(), timers_.end(),
        [](const Timer &left, const Timer &right) { return left.due < right.due; });

    // Milliseconds, not the duration's own unit: `Duration` is nanosecond based,
    // and handing its count back as a millisecond delay turns five seconds into
    // eight days.
    const auto until = std::chrono::duration_cast<std::chrono::milliseconds>(
                           nearest->due - std::chrono::steady_clock::now())
                           .count();
    if (until <= 0) {
        return 0;
    }
    // Exact deadline, not a capped slice: the pipe wakes us for new work.
    return static_cast<int>(std::min<std::int64_t>(until, 60'000));
}

void EngineBase::drainWakeup()
{
    if (wake_[0] < 0) {
        return;
    }
    char scratch[64];
    while (::read(wake_[0], scratch, sizeof scratch) > 0) {
    }
}

void EngineBase::drainUntil(std::chrono::steady_clock::time_point deadline)
{
    TaskNode *queue = takeTasks();
    bool ranAny = false;
    while (queue != nullptr) {
        // Between tasks, never inside one: a completion that stops half way
        // leaves torn state. The first task always runs, so a turn makes
        // progress even on an expired deadline.
        if (ranAny && std::chrono::steady_clock::now() >= deadline) {
            break;
        }

        const std::unique_ptr<TaskNode> node{queue};
        queue = node->next;

        const PostedTask task = std::move(node->task);
        task();
        ranAny = true;
    }

    if (queue == nullptr) {
        return;
    }
    // Put the unrun tail back at the top, keeping its order. The exchange hands
    // back whatever was pushed meanwhile, and it goes behind the tail.
    TaskNode *tail = queue;
    while (tail->next != nullptr) {
        tail = tail->next;
    }
    tail->next = head_.load(std::memory_order_relaxed);
    while (!head_.compare_exchange_weak(tail->next, queue, std::memory_order_release,
                                        std::memory_order_relaxed)) {
    }
}

void EngineBase::completeImmediately(PendingOp &op)
{
    const OpKind kind = op.kind;
    PendingOp *target = &op;
    post([this, target, kind] {
        switch (kind) {
        case OpKind::read:
            completeFileRead(*target);
            break;
        case OpKind::write:
            completeFileWrite(*target);
            break;
        case OpKind::connect:
            completeConnect(*target, {});
            break;
        case OpKind::accept:
            performAccept(*target);
            break;
        }
        release(target);
    });
}

void EngineBase::performRead(PendingOp &op)
{
    const ssize_t got = ::read(op.handle->native(), op.buffer.data(), op.buffer.size());
    if (got < 0) {
        completeRead(op, std::unexpected(errno == EAGAIN ? IoError::wouldBlock : IoError::system));
        return;
    }
    if (got == 0) {
        completeRead(op, std::unexpected(IoError::closed));
        return;
    }
    op.buffer.resize(static_cast<std::size_t>(got));
    completeRead(op, std::move(op.buffer));
}

void EngineBase::performWrite(PendingOp &op)
{
    const ssize_t put = ::write(op.handle->native(), op.buffer.data(), op.buffer.size());
    if (put < 0) {
        completeWrite(op, std::unexpected(errno == EAGAIN ? IoError::wouldBlock : IoError::system));
        return;
    }
    completeWrite(op, static_cast<std::size_t>(put));
}

void EngineBase::performAccept(PendingOp &op)
{
    const int accepted = ::accept(op.handle->native(), nullptr, nullptr);
    if (accepted < 0) {
        completeAccept(op, std::unexpected(errno == EMFILE || errno == ENFILE
                                               ? IoError::tooManyDescriptors
                                               : IoError::system));
        return;
    }
    completeAccept(op, accepted);
}

void EngineBase::performConnect(PendingOp &op)
{
    int pending = 0;
    socklen_t length = sizeof pending;
    if (::getsockopt(op.handle->native(), SOL_SOCKET, SO_ERROR, &pending, &length) != 0 || pending != 0) {
        completeConnect(op, std::unexpected(IoError::system));
        return;
    }
    completeConnect(op, {});
}

void EngineBase::completeFileRead(PendingOp &op)
{
    const ssize_t got = ::pread(op.handle->native(), op.buffer.data(), op.buffer.size(),
                                static_cast<off_t>(op.offset.count()));
    if (got < 0) {
        completeRead(op, std::unexpected(errno == EAGAIN ? IoError::wouldBlock : IoError::system));
        return;
    }
    op.buffer.resize(static_cast<std::size_t>(got));
    completeRead(op, got == 0 ? std::unexpected(IoError::closed) : std::expected<Buffer, IoError>{std::move(op.buffer)});
}

void EngineBase::completeFileWrite(PendingOp &op)
{
    const ssize_t put = ::pwrite(op.handle->native(), op.buffer.data(), op.buffer.size(),
                                 static_cast<off_t>(op.offset.count()));
    if (put < 0) {
        completeWrite(op, std::unexpected(IoError::system));
        return;
    }
    completeWrite(op, static_cast<std::size_t>(put));
}

void EngineBase::releaseLast()
{
    const std::lock_guard<std::recursive_mutex> guard{opsMutex_};
    ops_.pop_back();
}

void EngineBase::release(const PendingOp *op)
{
    // The slot makes this O(1): with a few thousand watches open, a scan per
    // completion is quadratic on the loop.
    const std::lock_guard<std::recursive_mutex> guard{opsMutex_};
    ops_.erase(op->slot);
}

void EngineBase::countCompletion(const IoError *failure) noexcept
{
    completed_.fetch_add(1, std::memory_order_relaxed);
    if (failure != nullptr) {
        failed_[failureIndex(*failure)].fetch_add(1, std::memory_order_relaxed);
    }
}

void EngineBase::completeRead(PendingOp &op, std::expected<Buffer, IoError> result)
{
    countCompletion(result.has_value() ? nullptr : &result.error());
    if (op.onRead) {
        op.onRead(std::move(result));
    }
}

void EngineBase::completeWrite(PendingOp &op, std::expected<std::size_t, IoError> result)
{
    countCompletion(result.has_value() ? nullptr : &result.error());
    if (op.onWrite) {
        op.onWrite(result);
    }
}

void EngineBase::completeAccept(PendingOp &op, std::expected<int, IoError> result)
{
    countCompletion(result.has_value() ? nullptr : &result.error());
    if (op.onAccept) {
        op.onAccept(result);
    }
}

void EngineBase::completeConnect(PendingOp &op, std::expected<void, IoError> result)
{
    countCompletion(result.has_value() ? nullptr : &result.error());
    if (op.onConnect) {
        op.onConnect(result);
    }
}

} // namespace antares::adapters::platform::io
