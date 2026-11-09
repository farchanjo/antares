#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "domain/json/value.hpp"
#include "domain/k8s/api_error.hpp"

namespace antares::domain::k8s {

/// What a frame announces (FR-004).
enum class EventType : std::uint8_t { added, modified, deleted, bookmark, error };

[[nodiscard]] std::string_view eventName(EventType type) noexcept;

/// One frame of a watch stream. The payload keeps its raw JSON so the caller
/// decodes only what it needs; a `BOOKMARK` carries no payload, only an
/// advanced `resourceVersion`.
struct WatchEvent {
    EventType type = EventType::added;
    std::string resourceVersion;
    json::Value payload;
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

/// Pull every complete newline-terminated frame out of `buffer`, leaving the
/// incomplete tail in place. A streaming body arrives in pieces that do not
/// align to frames, so the remainder must survive the chunk.
[[nodiscard]] std::vector<std::string> takeFrames(std::string &buffer);

} // namespace antares::domain::k8s
