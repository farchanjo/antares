#include "adapters/http/h1_session.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <expected>
#include <string_view>

namespace antares::adapters::http {
namespace {

std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

std::string_view lower(std::string_view text)
{
    static thread_local std::string scratch;
    scratch.assign(text.begin(), text.end());
    std::transform(scratch.begin(), scratch.end(), scratch.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return scratch;
}

} // namespace

H1Session::H1Session(domain::Bytes headerCeiling, domain::Bytes bodyCeiling,
                     domain::http::Method method)
    : headerCeiling_(headerCeiling)
    , bodyCeiling_(bodyCeiling)
    , method_(method)
{
}

std::string H1Session::serialize(const domain::http::Request &request, const std::string &target,
                                 const std::string &authority, const std::string &userAgent)
{
    std::string wire;
    wire.reserve(256 + request.body.size());
    wire += domain::http::methodName(request.method);
    wire += ' ';
    wire += target.empty() ? "/" : target;
    wire += " HTTP/1.1\r\nHost: ";
    wire += authority;
    wire += "\r\nUser-Agent: ";
    wire += userAgent;
    wire += "\r\nAccept: */*\r\nConnection: keep-alive\r\npriority: u=";
    wire += std::to_string(request.urgency());
    wire += "\r\n";

    for (const domain::http::Header &header : request.headers) {
        wire += header.name;
        wire += ": ";
        wire += header.value;
        wire += "\r\n";
    }

    wire += "Content-Length: ";
    wire += std::to_string(request.body.size());
    wire += "\r\n\r\n";
    wire.append(reinterpret_cast<const char *>(request.body.data()), request.body.size());
    return wire;
}

void H1Session::onHead(std::function<void(const domain::http::Response &)> callback)
{
    head_ = std::move(callback);
    streaming_ = true;
}

void H1Session::onChunk(std::function<void(std::span<const std::byte>)> callback)
{
    chunk_ = std::move(callback);
    streaming_ = true;
}

namespace {

void deliver(const std::function<void(std::span<const std::byte>)> &sink, const std::string &from,
             std::size_t offset, std::size_t length)
{
    if (sink && length > 0) {
        const auto *start = reinterpret_cast<const std::byte *>(from.data() + offset);
        sink(std::span<const std::byte>{start, length});
    }
}

} // namespace

std::expected<void, domain::http::HttpError> H1Session::consume(const std::byte *data,
                                                               std::size_t length)
{
    if (!headParsed_ && buffer_.size() + length > headerCeiling_.count()) {
        return std::unexpected(domain::http::HttpError::headerCeiling);
    }
    if (buffer_.size() + length > bodyCeiling_.count() + headerCeiling_.count()) {
        return std::unexpected(domain::http::HttpError::bodyCeiling);
    }

    buffer_.append(reinterpret_cast<const char *>(data), length);

    if (!headParsed_) {
        auto parsed = parseHead();
        if (!parsed) {
            return parsed;
        }
    }
    if (!headParsed_) {
        return {};
    }
    return parseBody();
}

std::expected<void, domain::http::HttpError> H1Session::parseHead()
{
    const std::size_t end = buffer_.find("\r\n\r\n");
    if (end == std::string::npos) {
        return {};
    }

    const std::string_view head{buffer_.data(), end};
    const std::size_t lineEnd = head.find("\r\n");
    const std::string_view statusLine = head.substr(0, lineEnd);

    const std::size_t firstSpace = statusLine.find(' ');
    if (statusLine.substr(0, 5) != "HTTP/" || firstSpace == std::string_view::npos) {
        return std::unexpected(domain::http::HttpError::protocol);
    }

    unsigned int status = 0;
    const std::string_view code = statusLine.substr(firstSpace + 1, 3);
    if (std::from_chars(code.data(), code.data() + code.size(), status).ec != std::errc{} ||
        status < 100 || status > 599) {
        return std::unexpected(domain::http::HttpError::protocol);
    }
    response_.status = static_cast<std::uint16_t>(status);
    response_.version = domain::http::Version::http11;

    std::size_t position = lineEnd == std::string_view::npos ? head.size() : lineEnd + 2;
    while (position < head.size()) {
        const std::size_t lineStop = head.find("\r\n", position);
        const std::string_view line =
            head.substr(position, lineStop == std::string_view::npos ? std::string_view::npos
                                                                    : lineStop - position);
        const std::size_t colon = line.find(':');
        if (colon != std::string_view::npos) {
            const std::string_view name = trim(line.substr(0, colon));
            const std::string_view value = trim(line.substr(colon + 1));

            if (lower(name) == "connection") {
                const std::string_view lowered = lower(value);
                reusable_ = lowered.find("close") == std::string_view::npos;
            }
            if (lower(name) == "content-length") {
                std::size_t declared = 0;
                if (std::from_chars(value.data(), value.data() + value.size(), declared).ec !=
                    std::errc{}) {
                    return std::unexpected(domain::http::HttpError::protocol);
                }
                remaining_ = declared;
            } else if (lower(name) == "transfer-encoding") {
                chunked_ = lower(value).find("chunked") != std::string_view::npos;
            }
            response_.headers.push_back(
                domain::http::Header{std::string{name}, std::string{value}});
        }
        if (lineStop == std::string_view::npos) {
            break;
        }
        position = lineStop + 2;
    }

    headParsed_ = true;
    cursor_ = end + 4;

    if (head_) {
        const domain::http::Response snapshot = response_;
        head_(snapshot);
    }

    // 204 and 304 carry no body; a 1xx is interim and the next head follows.
    if (response_.status == 204 || response_.status == 304) {
        complete_ = true;
    } else if (response_.status < 200) {
        response_.headers.clear();
        headParsed_ = false;
        cursor_ = 0;
        buffer_.erase(0, end + 4);
        return parseHead();
    } else if (!chunked_ && !remaining_) {
        remaining_ = 0;
    }
    if (method_ == domain::http::Method::head) {
        complete_ = true;
    }
    return {};
}

std::expected<void, domain::http::HttpError> H1Session::parseBody()
{
    if (complete_) {
        return {};
    }
    if (chunked_) {
        return parseChunked();
    }

    const std::size_t available = buffer_.size() - cursor_;
    const std::size_t wanted = remaining_.value_or(0);
    const std::size_t take = std::min(available, wanted);

    deliver(chunk_, buffer_, cursor_, take);
    if (!streaming_) {
        response_.body.insert(response_.body.end(),
                              reinterpret_cast<const std::byte *>(buffer_.data() + cursor_),
                              reinterpret_cast<const std::byte *>(buffer_.data() + cursor_ + take));
    }
    cursor_ += take;
    remaining_ = wanted - take;

    if (remaining_.value_or(0) == 0) {
        complete_ = true;
    }
    return {};
}

std::expected<void, domain::http::HttpError> H1Session::parseChunked()
{
    while (!complete_) {
        if (chunkRemaining_ == 0) {
            const std::size_t lineEnd = buffer_.find("\r\n", cursor_);
            if (lineEnd == std::string::npos) {
                return {};
            }

            std::size_t size = 0;
            const char *first = buffer_.data() + cursor_;
            const char *last = buffer_.data() + lineEnd;
            const auto parsed = std::from_chars(first, last, size, 16);
            if (parsed.ec != std::errc{}) {
                return std::unexpected(domain::http::HttpError::protocol);
            }
            cursor_ = lineEnd + 2;

            if (size == 0) {
                chunkDone_ = true;
                complete_ = true;
                return {};
            }
            chunkRemaining_ = size;
        }

        const std::size_t available = buffer_.size() - cursor_;
        if (available == 0) {
            return {};
        }

        const std::size_t take = std::min(available, chunkRemaining_);
        deliver(chunk_, buffer_, cursor_, take);
        if (!streaming_) {
            response_.body.insert(response_.body.end(),
                                  reinterpret_cast<const std::byte *>(buffer_.data() + cursor_),
                                  reinterpret_cast<const std::byte *>(buffer_.data() + cursor_ + take));
        }
        cursor_ += take;
        chunkRemaining_ -= take;

        if (chunkRemaining_ == 0 && buffer_.size() - cursor_ >= 2) {
            cursor_ += 2;
        }
    }
    return {};
}

domain::http::Response H1Session::take()
{
    return std::move(response_);
}

} // namespace antares::adapters::http
