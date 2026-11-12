#include "domain/http/sse.hpp"

#include <charconv>

#include <expected>

namespace antares::domain::http {
namespace {

constexpr std::string_view ID = "id";
constexpr std::string_view EVENT = "event";
constexpr std::string_view DATA = "data";
constexpr std::string_view RETRY = "retry";

std::string_view afterField(std::string_view line)
{
    if (!line.empty() && line.front() == ' ') {
        line.remove_prefix(1);
    }
    return line;
}

} // namespace

SseParser::SseParser(Bytes ceiling)
    : ceiling_(ceiling)
{
}

void SseParser::dispatch()
{
    // A block with no data line is not an event (the grammar requires data).
    if (!hasData_) {
        pending_ = SseEvent{};
        pending_.id = lastId_;
        return;
    }

    if (pending_.id.empty()) {
        pending_.id = lastId_;
    }
    lastId_ = pending_.id;
    ready_.push_back(std::move(pending_));

    pending_ = SseEvent{};
    pending_.id = lastId_;
    hasData_ = false;
}

std::expected<void, HttpError> SseParser::consumeLine(std::string_view line)
{
    if (line.empty()) {
        dispatch();
        return {};
    }
    if (line.front() == ':') {
        return {}; // comment
    }

    const std::size_t colon = line.find(':');
    const std::string_view field = colon == std::string_view::npos ? line : line.substr(0, colon);
    const std::string_view value = colon == std::string_view::npos
                                       ? std::string_view{}
                                       : afterField(line.substr(colon + 1));

    if (field == DATA) {
        if (hasData_) {
            pending_.data += '\n';
        }
        pending_.data.append(value);
        hasData_ = true;
        return {};
    }
    if (field == EVENT) {
        pending_.type.assign(value);
        return {};
    }
    if (field == ID) {
        if (value.find('\0') == std::string_view::npos) {
            pending_.id.assign(value);
        }
        return {};
    }
    if (field == RETRY) {
        std::uint32_t milliseconds = 0;
        if (std::from_chars(value.data(), value.data() + value.size(), milliseconds).ec ==
            std::errc{}) {
            pending_.retry = Duration{milliseconds};
        }
        return {};
    }
    return {};
}

std::expected<std::vector<SseEvent>, HttpError> SseParser::feed(const std::byte *data,
                                                                std::size_t length)
{
    if (done_) {
        return ready_;
    }
    if (buffer_.size() + length > ceiling_.count()) {
        return std::unexpected(HttpError::bodyCeiling);
    }
    buffer_.append(reinterpret_cast<const char *>(data), length);

    // A byte order mark opens the stream and is not part of the first line.
    if (!bomChecked_) {
        bomChecked_ = true;
        if (buffer_.size() >= 3 && buffer_.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            buffer_.erase(0, 3);
        }
    }

    std::size_t start = 0;
    while (true) {
        const std::size_t end = buffer_.find('\n', start);
        if (end == std::string::npos) {
            break;
        }

        std::string_view line{buffer_.data() + start, end - start};
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        auto consumed = consumeLine(line);
        if (!consumed) {
            return std::unexpected(consumed.error());
        }
        start = end + 1;
    }

    buffer_.erase(0, start);
    auto out = std::move(ready_);
    ready_.clear();
    return out;
}

} // namespace antares::domain::http
