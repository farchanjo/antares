#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "domain/http/http_error.hpp"
#include "domain/units/bytes.hpp"
#include "domain/units/duration.hpp"

namespace antares::domain::http {

/// One dispatched event. `data` has the trailing newline trimmed, per the
/// event-stream grammar.
struct SseEvent {
    std::string id;
    std::string type;
    std::string data;
    std::optional<Duration> retry;
};

/// Incremental parser for `text/event-stream` (WHATWG HTML §9.2).
///
/// Bytes go in, complete events come out. A partial event stays buffered, so a
/// chunk boundary never splits an event in a way the caller can see.
class SseParser {
public:
    explicit SseParser(Bytes ceiling);

    [[nodiscard]] std::expected<std::vector<SseEvent>, HttpError> feed(const std::byte *data,
                                                                     std::size_t length);

    /// The last `id:` seen, for the `Last-Event-ID` reconnect header.
    [[nodiscard]] const std::string &lastEventId() const noexcept { return lastId_; }

    [[nodiscard]] bool isDone() const noexcept { return done_; }

private:
    [[nodiscard]] std::expected<void, HttpError> consumeLine(std::string_view line);
    void dispatch();

    std::string buffer_;
    std::string lastId_;
    SseEvent pending_;
    std::vector<SseEvent> ready_;
    Bytes ceiling_;
    bool hasData_ = false;
    bool done_ = false;
};

} // namespace antares::domain::http
