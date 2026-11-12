#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "domain/json/value.hpp"
#include "domain/k8s/api_error.hpp"
#include "domain/k8s/entry.hpp"

namespace antares::domain::k8s {

/// What a frame announces (FR-004).
enum class EventType : std::uint8_t { added, modified, deleted, bookmark, error };

[[nodiscard]] std::string_view eventName(EventType type) noexcept;

/// One frame of a watch stream, already reduced to the projection.
///
/// The document the frame was parsed from is dropped inside the decoder, so a
/// frame retains an `Entry` and never a `json::Value` (FR-002, FR-005). A
/// `BOOKMARK` carries no entry, only an advanced `resourceVersion`.
struct WatchEvent {
    EventType type = EventType::added;
    std::string resourceVersion;
    std::optional<Entry> entry;
    ApiError failure;

    /// True when the frame surfaces a resource to the caller. A bookmark
    /// advances the version and emits nothing.
    [[nodiscard]] bool produces() const noexcept
    {
        return type == EventType::added || type == EventType::modified ||
               type == EventType::deleted;
    }
};

/// Decode one frame. A blank line is a keep-alive, not a failure, so the caller
/// skips it rather than tearing the stream down.
[[nodiscard]] std::expected<std::optional<WatchEvent>, ApiError>
decodeWatchEvent(std::string_view line);

/// The same over an already parsed document, for a caller that holds one.
[[nodiscard]] std::expected<std::optional<WatchEvent>, ApiError>
decodeWatchEvent(const json::Value &document);

/// A cursor over the stream accumulator: it hands out views of the complete
/// frames and remembers how far it read, so no frame is copied out (FR-005).
///
/// The buffer is compacted once, when the cursor is destroyed or `compact()` is
/// called — once per received chunk rather than once per frame. A view stays
/// valid until then, and the buffer must not be appended to while a view lives.
class FrameCursor {
public:
    explicit FrameCursor(std::string &buffer) noexcept;

    FrameCursor(const FrameCursor &) = delete;
    FrameCursor &operator=(const FrameCursor &) = delete;
    FrameCursor(FrameCursor &&) = delete;
    FrameCursor &operator=(FrameCursor &&) = delete;

    ~FrameCursor();

    /// The next complete frame without its newline, or nothing when the
    /// accumulator holds only an incomplete tail.
    [[nodiscard]] std::optional<std::string_view> next() noexcept;

    /// Bytes read so far, the incomplete tail excluded.
    [[nodiscard]] std::size_t consumed() const noexcept { return start_; }

    /// Erase the consumed prefix and return its length. Idempotent, so the
    /// destructor's call after an explicit one erases nothing.
    std::size_t compact() noexcept;

private:
    std::string &buffer_;
    std::size_t start_ = 0;
};

} // namespace antares::domain::k8s
