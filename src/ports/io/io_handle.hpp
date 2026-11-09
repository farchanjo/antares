#pragma once

#include <cstdint>
#include <expected>
#include <optional>

#include "ports/io/io_error.hpp"

namespace antares::ports::io {

/// How the engine must treat a descriptor (ADR 0008).
///
/// A regular file is always readable and always writable, so a readiness filter
/// fires on every sweep and spins the loop. Files therefore complete through an
/// immediate strategy; streams wait on readiness.
enum class HandleKind : std::uint8_t {
    stream,
    file,
    other,
};

enum class Access : std::uint8_t {
    read,
    write,
    readWrite,
};

/// Owning descriptor with its classification.
///
/// A caller obtains one and hands it to the engine. The caller never inspects
/// the kind and never branches on the platform: the classification happens once,
/// at adoption, and the engine selects the strategy from it.
class IoHandle {
public:
    IoHandle() = default;
    ~IoHandle();

    IoHandle(const IoHandle &) = delete;
    IoHandle &operator=(const IoHandle &) = delete;
    IoHandle(IoHandle && other) noexcept;
    IoHandle &operator=(IoHandle && other) noexcept;

    /// Take ownership of `descriptor` and classify it with `fstat`.
    [[nodiscard]] static std::expected<IoHandle, IoError> adopt(int descriptor) noexcept;

    /// Open a regular file and classify it.
    [[nodiscard]] static std::expected<IoHandle, IoError> openFile(const char *path,
                                                                  Access access) noexcept;

    [[nodiscard]] int native() const noexcept;
    [[nodiscard]] HandleKind kind() const noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool isFile() const noexcept;

    /// Current size of a regular file; zero for a stream.
    [[nodiscard]] std::uint64_t size() const noexcept;

    /// Give up ownership without closing.
    [[nodiscard]] int release() noexcept;

private:
    int descriptor_ = -1;
    HandleKind kind_ = HandleKind::other;
    std::uint64_t size_ = 0;
};

} // namespace antares::ports::io
