#include "domain/k8s/watch_event.hpp"

#include <expected>
#include <optional>
#include <string>
#include <utility>

#include "domain/json/scalar_scan.hpp"
#include "domain/k8s/entry_decoder.hpp"
#include "domain/k8s/status.hpp"
#include "domain/k8s/store_error.hpp"

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

/// An ERROR frame carries a Status in place of an object, so the code is the
/// only way to tell a 410 from a transient failure.
[[nodiscard]] ApiError failureFromStatus(const json::Value &status)
{
    Status parsed;
    parsed.state = std::string{status.textAt("status")};
    parsed.reasonToken = std::string{status.textAt("reason")};
    parsed.message = std::string{status.textAt("message")};
    parsed.code = static_cast<std::uint16_t>(status.intAt("code").value_or(0));

    ApiError failure = parsed.toError();
    // A 410 Expired is what makes a caller re-list; the token may be absent, so
    // the code stands in for it.
    if (failure.reason == Reason::unexpected && parsed.code == 410) {
        failure.reason = Reason::expired;
    }
    return failure;
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

std::expected<std::optional<WatchEvent>, ApiError> decodeWatchEvent(const json::Value &document)
{
    if (!document.isObject()) {
        return std::unexpected(
            ApiError{Reason::unexpected, 0, "watch frame is not a JSON object"});
    }

    const std::string_view token = document.textAt("type");
    const std::optional<EventType> type = typeFromToken(token);
    if (!type) {
        return std::unexpected(
            ApiError{Reason::unexpected, 0, "unknown watch frame type: " + std::string{token}});
    }

    const json::Value &object = document.member("object");

    WatchEvent event;
    event.type = *type;
    event.resourceVersion = std::string{object.member("metadata").textAt("resourceVersion")};

    if (event.type == EventType::error) {
        event.failure = failureFromStatus(object);
        return std::optional<WatchEvent>{std::move(event)};
    }
    if (event.type == EventType::bookmark) {
        return std::optional<WatchEvent>{std::move(event)};
    }

    auto entry = decodeEntry(object);
    if (!entry) {
        return std::unexpected(ApiError{Reason::unexpected, 0,
                                        std::string{storeErrorName(entry.error())}});
    }
    event.resourceVersion = entry->resourceVersion;
    event.entry = std::move(*entry);
    return std::optional<WatchEvent>{std::move(event)};
}

std::expected<std::optional<WatchEvent>, ApiError> decodeWatchEvent(std::string_view line,
                                                                   bool *scannerRefused)
{
    const std::string_view text = trimmed(line);
    if (text.empty()) {
        return std::optional<WatchEvent>{};
    }

    const auto malformed = []() {
        return std::unexpected(ApiError{Reason::unexpected, 0, "watch frame is not a JSON object"});
    };

    // The envelope is walked, not built: `type` is a short literal and `object`
    // is a byte range of the frame, so an ERROR status and a BOOKMARK are the
    // only frames that reach the parser (T010).
    json::ScalarScan scan{text};
    if (!scan.begin('{')) {
        return malformed();
    }

    std::string type;
    std::string_view object;
    bool first = true;
    while (true) {
        const auto state = scan.nextEntry('}', first);
        if (state == json::ScalarScan::EntryState::failed) {
            return malformed();
        }
        if (state == json::ScalarScan::EntryState::closed) {
            break;
        }
        first = false;

        std::string_view key;
        if (!scan.memberName(key)) {
            return malformed();
        }
        if (key == "type") {
            std::string_view value;
            if (!scan.stringValue(value)) {
                return malformed();
            }
            type = std::string{value};
            continue;
        }
        if (key == "object") {
            const std::size_t start = scan.offset();
            if (!scan.skipValue(1)) {
                return malformed();
            }
            object = text.substr(start, scan.offset() - start);
            continue;
        }
        if (!scan.skipValue(1)) {
            return malformed();
        }
    }

    const std::optional<EventType> kind = typeFromToken(type);
    if (!kind) {
        return std::unexpected(
            ApiError{Reason::unexpected, 0, "unknown watch frame type: " + type});
    }

    WatchEvent event;
    event.type = *kind;
    if (event.type == EventType::error) {
        const auto status = json::Value::parse(object);
        if (!status) {
            return malformed();
        }
        event.failure = failureFromStatus(*status);
        return std::optional<WatchEvent>{std::move(event)};
    }
    if (event.type == EventType::bookmark) {
        // A bookmark is a metadata-only object; parsing it is cheaper than a
        // second walk and it never carries a payload.
        const auto document = json::Value::parse(object);
        if (document) {
            event.resourceVersion = std::string{document->member("metadata").textAt("resourceVersion")};
        }
        return std::optional<WatchEvent>{std::move(event)};
    }

    auto entry = decodeEntry(object, scannerRefused);
    if (!entry) {
        return std::unexpected(ApiError{Reason::unexpected, 0,
                                        std::string{storeErrorName(entry.error())}});
    }
    event.resourceVersion = entry->resourceVersion;
    event.entry = std::move(*entry);
    return std::optional<WatchEvent>{std::move(event)};
}

FrameCursor::FrameCursor(std::string &buffer) noexcept : buffer_(buffer) {}

FrameCursor::~FrameCursor()
{
    static_cast<void>(compact());
}

std::optional<std::string_view> FrameCursor::next() noexcept
{
    const std::size_t newline = buffer_.find('\n', start_);
    if (newline == std::string::npos) {
        return std::nullopt;
    }
    const std::string_view frame{buffer_.data() + start_, newline - start_};
    start_ = newline + 1;
    return frame;
}

std::size_t FrameCursor::compact() noexcept
{
    const std::size_t erased = start_;
    if (erased > 0) {
        buffer_.erase(0, erased);
        start_ = 0;
    }
    return erased;
}

} // namespace antares::domain::k8s
