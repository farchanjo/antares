#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

#include "ports/io/io_error.hpp"
#include "ports/io/io_handle.hpp"

namespace antares::ports::io {

/// Bytes owned by the engine once submitted (ADR 0006).
using Buffer = std::vector<std::byte>;

/// Result handed to a completion callback.
template <typename T>
using CompletionResult = std::expected<T, IoError>;

/// `std::function` rather than `std::move_only_function`: libc++ 18 ships the
/// former and not the latter, and the constraint costs only a copyable capture.
using ReadCompletion = std::function<void(CompletionResult<Buffer>)>;
using WriteCompletion = std::function<void(CompletionResult<std::size_t>)>;
using AcceptCompletion = std::function<void(CompletionResult<int>)>;
using ConnectCompletion = std::function<void(CompletionResult<void>)>;
using PostedTask = std::function<void()>;

/// Completion-oriented kernel I/O port.
///
/// The engine owns one thread. Callbacks run on it, never on the submitting
/// thread, and never after `stop()` returns. The port owns every buffer from
/// submission until completion, so a caller may leave scope immediately.
class IoEngine {
public:
    IoEngine() = default;
    IoEngine(const IoEngine &) = delete;
    IoEngine &operator=(const IoEngine &) = delete;
    IoEngine(IoEngine &&) = delete;
    IoEngine &operator=(IoEngine &&) = delete;
    virtual ~IoEngine() = default;

    /// Backend name, for logs and the conformance report. Never branched on.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /// Read at most `maxBytes` into an engine-owned buffer.
    ///
    /// `offset` is required for a file handle (positional read) and ignored for
    /// a stream. The engine picks the strategy from `handle.kind()`; the caller
    /// never branches.
    [[nodiscard]] virtual std::expected<void, IoError>
    submitRead(const IoHandle &handle, std::optional<std::uint64_t> offset, std::size_t maxBytes,
               ReadCompletion done) = 0;

    /// Write the whole buffer, then release it to the completion.
    [[nodiscard]] virtual std::expected<void, IoError>
    submitWrite(const IoHandle &handle, std::optional<std::uint64_t> offset, Buffer bytes,
                WriteCompletion done) = 0;

    /// Accept one connection on a listening handle.
    [[nodiscard]] virtual std::expected<void, IoError>
    submitAccept(const IoHandle &listener, AcceptCompletion done) = 0;

    /// Connect a handle to `address` with `length` bytes of sockaddr.
    [[nodiscard]] virtual std::expected<void, IoError>
    submitConnect(const IoHandle &handle, const void *address, std::uint32_t length,
                  ConnectCompletion done) = 0;

    /// Enqueue work onto the engine thread.
    virtual void post(PostedTask task) = 0;

    /// Run the loop on the calling thread until `stop()`.
    virtual void run() = 0;

    /// Stop the loop; no callback runs after this returns.
    virtual void stop() = 0;
};

} // namespace antares::ports::io
