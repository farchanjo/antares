#pragma once

#include <atomic>
#include <expected>
#include <thread>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <list>
#include <mutex>
#include <string>
#include <memory>
#include <vector>

#include "domain/units/instant.hpp"

#include "ports/io/io_engine.hpp"

namespace antares::adapters::platform::io {

using ports::io::AcceptCompletion;
using ports::io::Buffer;
using ports::io::ConnectCompletion;
using ports::io::EngineStats;
using ports::io::failureIndex;
using ports::io::FAILURE_FAMILIES;
using ports::io::IoEngine;
using ports::io::IoError;
using ports::io::IoHandle;
using domain::Bytes;
using ports::io::PostedTask;
using ports::io::ReadCompletion;
using ports::io::WriteCompletion;

enum class OpKind : std::uint8_t {
    read,
    write,
    accept,
    connect,
};

/// One in-flight operation. Address is stable: a backend stores it in `udata`.
struct PendingOp {
    /// Where the op sits in the store, so releasing it is O(1) instead of a
    /// scan. Filled in by `allocate`; only the engine reads it.
    std::list<std::unique_ptr<PendingOp>>::iterator slot;

    OpKind kind = OpKind::read;
    const IoHandle *handle = nullptr;
    Buffer buffer;
    Bytes offset{};

    /// False for the immediate strategy: the op never reaches a readiness
    /// filter, so a sweep must not pick it again.
    bool readiness = true;

    ReadCompletion onRead;
    WriteCompletion onWrite;
    AcceptCompletion onAccept;
    ConnectCompletion onConnect;
};

/// Machinery shared by every backend: the op store, the wakeup pipe, the posted
/// task queue and the loop. A backend supplies only how it waits.
class EngineBase : public IoEngine {
public:
    EngineBase();
    ~EngineBase() override;

    [[nodiscard]] std::expected<void, IoError>
    submitRead(const IoHandle &handle, std::optional<Bytes> offset, Bytes maxBytes,
               ReadCompletion done) override;
    [[nodiscard]] std::expected<void, IoError>
    submitWrite(const IoHandle &handle, std::optional<Bytes> offset, Buffer bytes,
                WriteCompletion done) override;
    [[nodiscard]] std::expected<void, IoError>
    submitAccept(const IoHandle &listener, AcceptCompletion done) override;
    [[nodiscard]] std::expected<void, IoError>
    submitConnect(const IoHandle &handle, std::span<const std::byte> address,
                  ConnectCompletion done) override;

    [[nodiscard]] EngineStats stats() const noexcept override;
    [[nodiscard]] bool isEngineThread() const noexcept override;

    [[nodiscard]] std::expected<void, IoError> submitTimer(domain::Duration delay,
                                                          PostedTask task) override;

    [[nodiscard]] std::expected<void, IoError> submitResolve(std::string host, std::uint16_t port,
                                                            ResolveDone done) override;

    void cancelPending(const IoHandle &handle) override;

    void post(PostedTask task) override;
    void run() override;
    /// Idempotent. After it returns, no completion fires until `run()` again.
    void stop() override;

protected:
    /// Wait for readiness. Returns true when at least one op made progress.
    /// Implemented by the backend; the timeout bounds an idle loop. The wait
    /// must cover the wakeup pipe, otherwise an idle engine spins.
    virtual bool waitReady(int timeoutMilliseconds) = 0;

    /// Register the wakeup pipe with the backend's wait set, once per run.
    virtual void armWakeup() = 0;

    /// Ask the backend to observe `op`. False means the backend cannot, and the
    /// op completes through the immediate strategy instead.
    virtual bool armRead(PendingOp &op) = 0;
    virtual bool armWrite(PendingOp &op) = 0;
    virtual bool armAccept(PendingOp &op) = 0;
    virtual bool armConnect(PendingOp &op) = 0;

    /// Drop any interest the backend still holds for `op`. Called before the op
    /// is released, so a queued readiness event cannot land on freed memory.
    virtual void forget(PendingOp &op);

    void countCompletion(const IoError *failure) noexcept;

    void completeRead(PendingOp &op, std::expected<Buffer, IoError> result);
    void completeWrite(PendingOp &op, std::expected<std::size_t, IoError> result);
    void completeAccept(PendingOp &op, std::expected<int, IoError> result);
    void completeConnect(PendingOp &op, std::expected<void, IoError> result);

    /// Complete `op` on the engine thread through the posted queue.
    void completeImmediately(PendingOp &op);

    /// Perform the syscall for a ready op and complete it. Shared by backends
    /// so a readiness backend only reports readiness, never I/O.
    void performRead(PendingOp &op);
    void performWrite(PendingOp &op);
    void performAccept(PendingOp &op);
    void performConnect(PendingOp &op);

    [[nodiscard]] int wakeReader() const noexcept;
    [[nodiscard]] std::size_t pendingCount() const noexcept;

    /// Make an idle wait return, so a submit from another thread is seen.
    void poke() noexcept;

    PendingOp *allocate(OpKind kind, const IoHandle &handle);

    /// Drop a settled op. Call after its completion ran.
    void release(const PendingOp *op);

    /// Drop the most recently allocated op, for a submit that could not arm it.
    void releaseLast();

    /// Guarded: a caller thread allocates while the loop releases, and an
    /// unguarded push_back reallocates under the loop's open iterator.
    mutable std::mutex opsMutex_;
    std::list<std::unique_ptr<PendingOp>> ops_;

private:
    void drainWakeup();
    void fireDueTimers();
    [[nodiscard]] int nextWaitMilliseconds() const;
    void drainTasks();
    void completeFileRead(PendingOp &op);
    void completeFileWrite(PendingOp &op);

    mutable std::atomic<std::uint64_t> submitted_{0};
    mutable std::atomic<std::uint64_t> completed_{0};
    mutable std::array<std::atomic<std::uint64_t>, FAILURE_FAMILIES> failed_{};

    struct Timer {
        domain::Instant due;
        PostedTask task;
    };

    /// Guarded: a caller thread arms a timer while the loop walks the same
    /// vector, and a reallocation under an open reference is a use-after-free.
    std::vector<Timer> timers_;
    mutable std::mutex timerMutex_;
    /// How long a name may take before the request is failed. `getaddrinfo`
    /// blocks and has no timeout of its own, so a broken resolver would
    /// otherwise pend the request for the life of the process.
    static constexpr domain::Duration resolveTimeout = std::chrono::seconds{5};

    struct ResolveJob {
        std::uint64_t serial = 0;
        std::string host;
        std::uint16_t port = 0;
        /// Shared: the resolver thread and its timer both may settle the job,
        /// and whichever runs second must find the callback still alive.
        std::shared_ptr<ResolveDone> done;
    };

    /// Claim `serial` for settling. True exactly once per job, so a late
    /// resolver answer cannot complete a job the timer already failed.
    [[nodiscard]] bool claimResolve(std::uint64_t serial);

    void startResolver();
    void resolverLoop();

    std::mutex resolveMutex_;
    std::condition_variable resolveReady_;
    std::deque<ResolveJob> resolveQueue_;
    std::thread resolver_;
    bool resolveStop_ = false;
    std::atomic<std::uint64_t> resolveSerial_{0};
    std::atomic<std::uint64_t> resolveSettled_{0};

    std::deque<PostedTask> tasks_;
    std::mutex mutex_;
    std::atomic<bool> stopped_{false};
    std::atomic<std::thread::id> loopThread_{};
    int wake_[2] = {-1, -1};
};

} // namespace antares::adapters::platform::io
