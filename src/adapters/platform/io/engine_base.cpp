#include "adapters/platform/io/engine_base.hpp"

#include <cerrno>
#include <fcntl.h>
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
                                                     std::optional<std::uint64_t> offset,
                                                     std::size_t maxBytes, ReadCompletion done)
{
    if (!handle.valid() || maxBytes == 0) {
        return std::unexpected(IoError::system);
    }

    PendingOp *op = allocate(OpKind::read, handle);
    op->offset = offset.value_or(0);
    op->buffer.resize(maxBytes);
    op->onRead = std::move(done);

    if (handle.isFile()) {
        completeImmediately(*op);
        return {};
    }

    if (!armRead(*op)) {
        op->onRead = nullptr;
        ops_.pop_back();
        return std::unexpected(IoError::tooManyDescriptors);
    }
    return {};
}

std::expected<void, IoError> EngineBase::submitWrite(const IoHandle &handle,
                                                      std::optional<std::uint64_t> offset,
                                                      Buffer bytes, WriteCompletion done)
{
    if (!handle.valid() || bytes.empty()) {
        return std::unexpected(IoError::system);
    }

    PendingOp *op = allocate(OpKind::write, handle);
    op->offset = offset.value_or(0);
    op->buffer = std::move(bytes);
    op->onWrite = std::move(done);

    if (handle.isFile()) {
        completeImmediately(*op);
        return {};
    }

    if (!armWrite(*op)) {
        op->onWrite = nullptr;
        ops_.pop_back();
        return std::unexpected(IoError::tooManyDescriptors);
    }
    return {};
}

std::expected<void, IoError> EngineBase::submitAccept(const IoHandle &listener,
                                                       AcceptCompletion done)
{
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
    return {};
}

std::expected<void, IoError> EngineBase::submitConnect(const IoHandle &handle, const void *address,
                                                        std::uint32_t length,
                                                        ConnectCompletion done)
{
    (void)address;
    (void)length;
    if (!handle.valid()) {
        return std::unexpected(IoError::system);
    }

    PendingOp *op = allocate(OpKind::connect, handle);
    op->onConnect = std::move(done);

    if (!armConnect(*op)) {
        op->onConnect = nullptr;
        ops_.pop_back();
        return std::unexpected(IoError::tooManyDescriptors);
    }
    return {};
}

void EngineBase::post(PostedTask task)
{
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        tasks_.push_back(std::move(task));
    }
    if (wake_[1] >= 0) {
        const char byte = 1;
        (void)::write(wake_[1], &byte, 1);
    }
}

void EngineBase::run()
{
    stopped_.store(false);
    while (!stopped_.load()) {
        drainWakeup();
        drainTasks();
        if (stopped_.load()) {
            break;
        }
        (void)waitReady(POLL_TIMEOUT_MS);
    }
}

void EngineBase::stop()
{
    if (stopped_.exchange(true)) {
        return;
    }
    if (wake_[1] >= 0) {
        const char byte = 1;
        (void)::write(wake_[1], &byte, 1);
    }
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
    const bool isRead = op.kind == OpKind::read;
    PendingOp *target = &op;
    post([this, target, isRead] {
        if (isRead) {
            completeFileRead(*target);
        } else {
            completeFileWrite(*target);
        }
    });
}

void EngineBase::completeFileRead(PendingOp &op)
{
    const ssize_t got = ::pread(op.handle->native(), op.buffer.data(), op.buffer.size(),
                                static_cast<off_t>(op.offset));
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
                                 static_cast<off_t>(op.offset));
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

void EngineBase::completeRead(PendingOp &op, std::expected<Buffer, IoError> result)
{
    if (op.onRead) {
        op.onRead(std::move(result));
    }
}

void EngineBase::completeWrite(PendingOp &op, std::expected<std::size_t, IoError> result)
{
    if (op.onWrite) {
        op.onWrite(result);
    }
}

void EngineBase::completeAccept(PendingOp &op, std::expected<int, IoError> result)
{
    if (op.onAccept) {
        op.onAccept(result);
    }
}

void EngineBase::completeConnect(PendingOp &op, std::expected<void, IoError> result)
{
    if (op.onConnect) {
        op.onConnect(result);
    }
}

} // namespace antares::adapters::platform::io
