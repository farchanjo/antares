#include "domain/k8s/watch_event.hpp"

#include <expected>
#include <utility>

#include "domain/k8s/status.hpp"

namespace antares::domain::k8s {

namespace {

[[nodiscard]] std::optional<EventType> typeFromToken(std::string_view token) noexcept
{
    if (token == "ADDED") {
        return EventType::added;
    }
    if (token == "MODIFIED") {
        return EventType::modified;
    }
    if (token == "DELETED") {
        return EventType::deleted;
    }
    if (token == "BOOKMARK") {
        return EventType::bookmark;
    }
    if (token == "ERROR") {
        return EventType::error;
    }
    return std::nullopt;
}

/// Windows line endings survive a proxy, so `\r`, `\n` and spaces are all
/// stripped: what remains is either a frame or nothing.
[[nodiscard]] bool isBlank(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

[[nodiscard]] std::string_view trimmed(std::string_view line) noexcept
{
    while (!line.empty() && isBlank(line.back())) {
        line.remove_suffix(1);
    }
    while (!line.empty() && isBlank(line.front())) {
        line.remove_prefix(1);
    }
    return line;
}

} // namespace

std::string_view eventName(EventType type) noexcept
{
    switch (type) {
    case EventType::added:
        return "ADDED";
    case EventType::modified:
        return "MODIFIED";
    case EventType::deleted:
        return "DELETED";
    case EventType::bookmark:
        return "BOOKMARK";
    case EventType::error:
        return "ERROR";
    }
    return "ADDED";
}

std::expected<std::optional<WatchEvent>, ApiError> decodeWatchEvent(std::string_view line)
{
    const std::string_view text = trimmed(line);
    if (text.empty()) {
        return std::optional<WatchEvent>{};
    }

    const auto document = json::Value::parse(text);
    if (!document || !document->isObject()) {
        return std::unexpected(
            ApiError{Reason::unexpected, 0, "watch frame is not a JSON object"});
    }

    const std::string_view token = document->textAt("type");
    const std::optional<EventType> type = typeFromToken(token);
    if (!type) {
        return std::unexpected(
            ApiError{Reason::unexpected, 0, "unknown watch frame type: " + std::string{token}});
    }

    WatchEvent event;
    event.type = *type;
    event.payload = document->member("object");
    event.resourceVersion =
        std::string{event.payload.member("metadata").textAt("resourceVersion")};

    if (event.type == EventType::error) {
        // An ERROR frame carries a Status in place of an object, so the code is
        // the only way to tell a 410 from a transient failure.
        Status status;
        status.state = std::string{event.payload.textAt("status")};
        status.reasonToken = std::string{event.payload.textAt("reason")};
        status.message = std::string{event.payload.textAt("message")};
        status.code = static_cast<std::uint16_t>(event.payload.intAt("code").value_or(0));
        event.failure = status.toError();
        // A 410 Expired is what makes a caller re-list; the token may be absent,
        // so the code stands in for it.
        if (event.failure.reason == Reason::unexpected && status.code == 410) {
            event.failure.reason = Reason::expired;
        }
    }

    return std::optional<WatchEvent>{std::move(event)};
}

std::vector<std::string> takeFrames(std::string &buffer)
{
    std::vector<std::string> frames;
    std::size_t start = 0;

    while (true) {
        const std::size_t newline = buffer.find('\n', start);
        if (newline == std::string::npos) {
            break;
        }
        frames.emplace_back(buffer.substr(start, newline - start));
        start = newline + 1;
    }

    buffer.erase(0, start);
    return frames;
}

} // namespace antares::domain::k8s
