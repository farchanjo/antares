#include "domain/json/sequence.hpp"

#include <algorithm>
#include <cstddef>

namespace antares::domain::json {
namespace {

constexpr char RECORD_SEPARATOR = '\x1e';
constexpr char LINE_FEED = '\n';
constexpr std::string_view WHITESPACE = " \t\r\n";

[[nodiscard]] bool allWhitespace(std::string_view text) noexcept
{
    return std::ranges::all_of(text, [](char octet) {
        return WHITESPACE.find(octet) != std::string_view::npos;
    });
}

} // namespace

bool isSelfDelimiting(std::string_view text) noexcept
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    if (text.empty()) {
        return false;
    }
    // Objects, arrays, and strings close themselves; nothing else does.
    return text.front() == '{' || text.front() == '[' || text.front() == '"';
}

std::vector<std::string> SequenceReader::feed(std::string_view chunk)
{
    buffer_.append(chunk);

    std::vector<std::string> texts;
    while (true) {
        const std::size_t marker = buffer_.find(RECORD_SEPARATOR);
        if (marker == std::string::npos) {
            break;
        }
        // Anything before the first RS is not part of a sequence element.
        if (marker > 0) {
            buffer_.erase(0, marker);
        }
        if (buffer_.size() < 2) {
            break;
        }

        // The element is delimited by the next RS. The last element in a stream
        // has none, so the LF that ends it is what proves it complete.
        std::size_t next = buffer_.find(RECORD_SEPARATOR, 1);
        if (next == std::string::npos) {
            const std::size_t lineFeed = buffer_.find(LINE_FEED, 1);
            if (lineFeed == std::string::npos || lineFeed + 1 != buffer_.size()) {
                break;
            }
            next = buffer_.size();
        }

        std::string_view text{buffer_.data() + 1, next - 1};
        if (!text.empty() && text.back() == LINE_FEED) {
            text.remove_suffix(1);
            if (!text.empty() && text.back() == '\r') {
                text.remove_suffix(1);
            }
            if (allWhitespace(text)) {
                // An empty element between two RS bytes denotes nothing.
                buffer_.erase(0, next);
                continue;
            }
            texts.emplace_back(text);
        } else if (!allWhitespace(text)) {
            // No LF: the encoder was cut mid-text. A self-delimited value still
            // parses; the rest is suspect.
            if (isSelfDelimiting(text)) {
                texts.emplace_back(text);
            } else {
                ++rejected_;
            }
        }
        buffer_.erase(0, next);
    }

    // A trailing text with no following RS is only complete if the stream ended
    // an element; keep it pending so the next feed can finish it.
    return texts;
}

} // namespace antares::domain::json
