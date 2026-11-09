#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "ports/io/io_engine.hpp"

namespace antares::adapters::platform::io {

using ports::io::AcceptCompletion;
using ports::io::Buffer;
using ports::io::ConnectCompletion;
using ports::io::IoEngine;
using ports::io::IoError;
using ports::io::IoHandle;
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
    OpKind kind = OpKind::read;
    const IoHandle *handle = nullptr;
    Buffer buffer;
    std::uint64_t offset = 0;

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
    submitRead(const IoHandle &handle, std::optional<std::uint64_t> offset, std::size_t maxBytes,
               ReadCompletion done) override;
    [[nodiscard]] std::expected<void, IoError>
    submitWrite(const IoHandle &handle, std::optional<std::uint64_t> offset, Buffer bytes,
                WriteCompletion done) override;
    [[nodiscard]] std::expected<void, IoError>
    submitAccept(const IoHandle &listener, AcceptCompletion done) override;
    [[nodiscard]] std::expected<void, IoError>
    submitConnect(const IoHandle &handle, std::span<const std::byte> address,
                  ConnectCompletion done) override;

    void post(PostedTask task) override;
    void run() override;
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

    std::deque<std::unique_ptr<PendingOp>> ops_;

private:
    void drainWakeup();
    void drainTasks();
    void completeFileRead(PendingOp &op);
    void completeFileWrite(PendingOp &op);

    std::deque<PostedTask> tasks_;
    std::mutex mutex_;
    std::atomic<bool> stopped_{false};
    int wake_[2] = {-1, -1};
};

} // namespace antares::adapters::platform::io
