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
#include <fcntl.h>
#include <sys/socket.h>

#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>

namespace antares::adapters::platform::io {
namespace {

constexpr int POLL_TIMEOUT_MS = 50;
constexpr std::size_t READ_CHUNK = 64 * 1024;

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
    if (wake_[0] >= 0) {
        ::close(wake_[0]);
    }
    if (wake_[1] >= 0) {
        ::close(wake_[1]);
    }
}

int EngineBase::wakeReader() const noexcept { return wake_[0]; }

std::size_t EngineBase::pendingCount() const noexcept { return ops_.size(); }

PendingOp *EngineBase::allocate(OpKind kind, const IoHandle &handle)
{
    auto op = std::make_unique<PendingOp>();
    op->kind = kind;
    op->handle = &handle;
    ops_.push_back(std::move(op));
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

    PendingOp *op = allocate(OpKind::read, handle);
    op->offset = offset.value_or(Bytes{});
    op->buffer.resize(maxBytes.count());
    op->onRead = std::move(done);

    if (handle.isFile()) {
        op->readiness = false;
        completeImmediately(*op);
        return {};
    }

    if (!armRead(*op)) {
        op->onRead = nullptr;
        ops_.pop_back();
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

    PendingOp *op = allocate(OpKind::write, handle);
    op->offset = offset.value_or(Bytes{});
    op->buffer = std::move(bytes);
    op->onWrite = std::move(done);

    if (handle.isFile()) {
        op->readiness = false;
        completeImmediately(*op);
        return {};
    }

    if (!armWrite(*op)) {
        op->onWrite = nullptr;
        ops_.pop_back();
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

    PendingOp *op = allocate(OpKind::accept, listener);
    op->onAccept = std::move(done);

    if (!armAccept(*op)) {
        op->onAccept = nullptr;
        ops_.pop_back();
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

    PendingOp *op = allocate(OpKind::connect, handle);
    op->onConnect = std::move(done);

    if (::connect(descriptor, reinterpret_cast<const sockaddr *>(address.data()), address.size()) ==
        0) {
        op->readiness = false;
        completeImmediately(*op);
        return {};
    }
    if (errno != EINPROGRESS) {
        op->readiness = false;
        post([this, op] { completeConnect(*op, std::unexpected(IoError::connect)); release(op); });
        return {};
    }

    if (!armConnect(*op)) {
        op->onConnect = nullptr;
        ops_.pop_back();
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
}

std::expected<void, IoError> EngineBase::submitTimer(domain::Duration delay, PostedTask task)
{
    if (!task) {
        return std::unexpected(IoError::system);
    }

    timers_.push_back(Timer{domain::afterFromNow(delay), std::move(task)});
    poke();
    return {};
}

std::expected<void, IoError> EngineBase::submitResolve(std::string host, std::uint16_t port,
                                                       ResolveDone done)
{
    if (!done || host.empty()) {
        return std::unexpected(IoError::system);
    }

    {
        const std::lock_guard<std::mutex> guard{resolveMutex_};
        resolveQueue_.push_back(ResolveJob{std::move(host), port, std::move(done)});
    }
    resolveReady_.notify_one();
    startResolver();
    return {};
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
        post([done = std::move(job.done), result = std::move(result)]() mutable {
            done(std::move(result));
        });
    }
}

void EngineBase::cancelPending(const IoHandle &handle)
{
    // Completed on the loop, like any other op, so a callback never runs from
    // the caller's thread.
    post([this, descriptor = handle.native()] {
        std::vector<PendingOp *> doomed;
        for (const auto &op : ops_) {
            if (op->handle != nullptr && op->handle->native() == descriptor) {
                doomed.push_back(op.get());
            }
        }

        for (PendingOp *op : doomed) {
            switch (op->kind) {
            case OpKind::read:
                completeRead(*op, std::unexpected(IoError::cancelled));
                break;
            case OpKind::write:
                completeWrite(*op, std::unexpected(IoError::cancelled));
                break;
            case OpKind::connect:
                completeConnect(*op, std::unexpected(IoError::cancelled));
                break;
            case OpKind::accept:
                completeAccept(*op, std::unexpected(IoError::cancelled));
                break;
            }
            release(op);
        }
    });
}

void EngineBase::post(PostedTask task)
{
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        tasks_.push_back(std::move(task));
    }
    poke();
}

void EngineBase::run()
{
    // Do not reset the flag here: a stop() issued before the thread reaches the
    // loop must survive, or the loop runs forever.
    tuning::tuneLoopThread("antares.io");
    loopThread_.store(std::this_thread::get_id(), std::memory_order_relaxed);
    armWakeup();
    while (!stopped_.load()) {
        drainWakeup();
        drainTasks();
        fireDueTimers();
        if (stopped_.load()) {
            break;
        }
        (void)waitReady(nextWaitMilliseconds());
    }
}

void EngineBase::stop()
{
    // Completing callbacks run on the loop thread only, so once `run()` is back
    // no completion can start. Pending operations are dropped, not completed.
    if (stopped_.exchange(true)) {
        return;
    }
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
}

void EngineBase::fireDueTimers()
{
    if (timers_.empty()) {
        return;
    }

    const domain::Instant now = std::chrono::steady_clock::now();
    std::vector<Timer> pending;
    pending.reserve(timers_.size());

    for (Timer &timer : timers_) {
        if (timer.due <= now) {
            tasks_.push_back(std::move(timer.task));
            continue;
        }
        pending.push_back(std::move(timer));
    }
    timers_.swap(pending);
}

int EngineBase::nextWaitMilliseconds() const
{
    // A queued task must run before any wait, or it sits until the next event.
    if (!tasks_.empty()) {
        return 0;
    }

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

    const auto until = std::chrono::duration_cast<domain::Duration>(
        nearest->due - std::chrono::steady_clock::now());
    if (until.count() <= 0) {
        return 0;
    }
    // Exact deadline, not a capped slice: the pipe wakes us for new work.
    return static_cast<int>(until.count());
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

void EngineBase::drainTasks()
{
    std::deque<PostedTask> ready;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        ready.swap(tasks_);
    }
    for (auto &task : ready) {
        task();
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

void EngineBase::release(const PendingOp *op)
{
    for (auto it = ops_.begin(); it != ops_.end(); ++it) {
        if (it->get() == op) {
            ops_.erase(it);
            return;
        }
    }
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
