#include "adapters/http/content_decoder.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <ranges>
#include <vector>

#include <zlib.h>
#include <zstd.h>

namespace antares::adapters::http {
namespace {

/// Bounded so a lying peer cannot make the decoder allocate without limit. Ten
/// times the coded size is far past what any real body compresses to.
constexpr std::size_t RATIO_CEILING = 32;
constexpr std::size_t FLOOR_CEILING = std::size_t{1} << 20;

[[nodiscard]] std::size_t ceilingFor(std::size_t coded) noexcept
{
    return std::max(coded * RATIO_CEILING, FLOOR_CEILING);
}

/// zlib inflate over the whole buffer. `windowBits` picks the wrapper: 15 is
/// zlib (deflate) and 15 + 16 is gzip.
[[nodiscard]] std::expected<std::string, domain::http::HttpError>
inflate(std::string_view coded, int windowBits)
{
    z_stream stream{};
    if (inflateInit2(&stream, windowBits) != Z_OK) {
        return std::unexpected(domain::http::HttpError::protocol);
    }

    std::string out;
    out.reserve(coded.size() * std::size_t{2});
    const std::size_t ceiling = ceilingFor(coded.size());
    std::array<char, std::size_t{16} * 1024> chunk{};

    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(coded.data()));
    stream.avail_in = static_cast<uInt>(coded.size());

    while (true) {
        stream.next_out = reinterpret_cast<Bytef *>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunk.size());
        const int status = inflate(&stream, Z_NO_FLUSH);

        out.append(chunk.data(), chunk.size() - stream.avail_out);
        if (out.size() > ceiling) {
            (void)inflateEnd(&stream);
            return std::unexpected(domain::http::HttpError::protocol);
        }
        if (status == Z_STREAM_END) {
            // RFC 1952 §2.2: a gzip stream is a series of members, so one
            // Z_STREAM_END is not the end of the body. Reset and keep going
            // while input remains; anything after the last member is a new one.
            if (stream.avail_in == 0) {
                break;
            }
            if (inflateReset2(&stream, windowBits) != Z_OK) {
                (void)inflateEnd(&stream);
                return std::unexpected(domain::http::HttpError::protocol);
            }
            continue;
        }
        if (status != Z_OK) {
            (void)inflateEnd(&stream);
            return std::unexpected(domain::http::HttpError::protocol);
        }
        if (stream.avail_in == 0 && stream.avail_out == chunk.size()) {
            // No progress and no input left: the peer truncated the stream.
            (void)inflateEnd(&stream);
            return std::unexpected(domain::http::HttpError::protocol);
        }
    }
    (void)inflateEnd(&stream);
    return out;
}

} // namespace

namespace {

[[nodiscard]] ContentEncoding codingOf(std::string_view token) noexcept
{
    const auto equals = [token](std::string_view wanted) {
        return token.size() == wanted.size() &&
               std::equal(token.begin(), token.end(), wanted.begin(), [](char left, char right) {
                   return std::tolower(static_cast<unsigned char>(left)) == right;
               });
    };

    if (token.empty() || equals("identity")) {
        return ContentEncoding::identity;
    }
    if (equals("gzip")) {
        return ContentEncoding::gzip;
    }
    if (equals("deflate")) {
        return ContentEncoding::deflate;
    }
    if (equals("zstd")) {
        return ContentEncoding::zstd;
    }
    return ContentEncoding::unknown;
}

} // namespace

std::vector<ContentEncoding> parseEncodings(std::string_view value)
{
    std::vector<ContentEncoding> codings;
    std::size_t start = 0;

    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        std::string_view token = value.substr(
            start, comma == std::string_view::npos ? std::string_view::npos : comma - start);

        // RFC 9110 §5.6.3: OWS around a list member is not part of the member.
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) {
            token.remove_prefix(1);
        }
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
            token.remove_suffix(1);
        }
        if (const ContentEncoding coding = codingOf(token); coding != ContentEncoding::identity) {
            codings.push_back(coding);
        }

        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return codings;
}

std::string_view acceptedEncodings() noexcept
{
    return "gzip, deflate, zstd, identity";
}

namespace {

std::expected<std::string, domain::http::HttpError> decodeOne(ContentEncoding encoding,
                                                             std::string_view coded)
{

    switch (encoding) {
    case ContentEncoding::gzip:
        return inflate(coded, 15 + 16);
    case ContentEncoding::deflate:
        // A bare `deflate` is meant to be zlib-wrapped, but peers send raw
        // deflate often enough that the fallback is worth the retry.
        if (auto wrapped = inflate(coded, 15); wrapped) {
            return wrapped;
        }
        return inflate(coded, -15);
    case ContentEncoding::zstd: {
        const unsigned long long declared = ZSTD_getFrameContentSize(coded.data(), coded.size());
        if (declared == ZSTD_CONTENTSIZE_ERROR) {
            return std::unexpected(domain::http::HttpError::protocol);
        }

        std::string out;
        if (declared != ZSTD_CONTENTSIZE_UNKNOWN) {
            if (declared > ceilingFor(coded.size())) {
                return std::unexpected(domain::http::HttpError::protocol);
            }
            out.resize(static_cast<std::size_t>(declared));
        } else {
            out.resize(ceilingFor(coded.size()));
        }

        const std::size_t written = ZSTD_decompress(out.data(), out.size(), coded.data(), coded.size());
        if (ZSTD_isError(written) != 0) {
            return std::unexpected(domain::http::HttpError::protocol);
        }
        out.resize(written);
        return out;
    }
    case ContentEncoding::unknown:
    case ContentEncoding::identity:
        break;
    }
    return std::string{coded};
}

} // namespace

std::expected<std::string, domain::http::HttpError>
decodeBody(const std::vector<ContentEncoding> &encodings, std::string_view coded)
{
    if (encodings.empty() || coded.empty()) {
        return std::string{coded};
    }

    // The sender applied them in the listed order, so the last one comes off first.
    std::string current{coded};
    for (const ContentEncoding coding : std::views::reverse(encodings)) {
        auto step = decodeOne(coding, current);
        if (!step) {
            return step;
        }
        current = std::move(*step);
    }
    return current;
}

} // namespace antares::adapters::http
