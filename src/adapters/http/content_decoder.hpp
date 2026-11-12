#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "domain/http/http_error.hpp"

namespace antares::adapters::http {

/// What the peer said the body is coded with, per RFC 9110 §8.4.1.
enum class ContentEncoding : unsigned char { identity, gzip, deflate, zstd, unknown };

/// Parse a `content-encoding` field value into the codings applied, in the order
/// the sender applied them. RFC 9110 §8.4: the field is a list, and a recipient
/// undoes it from the end. An empty or `identity`-only value yields an empty list.
[[nodiscard]] std::vector<ContentEncoding> parseEncodings(std::string_view value);

/// The `accept-encoding` this build offers. Brotli is absent from the list when
/// the decoder was not compiled in.
[[nodiscard]] std::string_view acceptedEncodings() noexcept;

/// Undo every coding on `coded`, last applied first.
[[nodiscard]] std::expected<std::string, domain::http::HttpError>
decodeBody(const std::vector<ContentEncoding> &encodings, std::string_view coded);

} // namespace antares::adapters::http
