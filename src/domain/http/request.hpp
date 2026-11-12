#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace antares::domain::http {

enum class Method : std::uint8_t { get, post, put, patch, del, head, options };

[[nodiscard]] constexpr std::string_view methodName(Method method) noexcept
{
    switch (method) {
    case Method::get:
        return "GET";
    case Method::post:
        return "POST";
    case Method::put:
        return "PUT";
    case Method::patch:
        return "PATCH";
    case Method::del:
        return "DELETE";
    case Method::head:
        return "HEAD";
    case Method::options:
        return "OPTIONS";
    }
    return "GET";
}

enum class Version : std::uint8_t { negotiate, http2, http11 };

struct Header {
    std::string name;
    std::string value;
};

/// Wire bytes are `std::byte`, never `char` (P-11).
using Body = std::vector<std::byte>;

/// One submission. `version` overrides ALPN and is test-only (FR-005).
struct Request {
    Method method = Method::get;

    /// RFC 9218 urgency, 0 (highest) .. 7. Sent as `priority: u=N`, clamped
    /// because the field only defines those eight values.
    std::uint8_t priority = 3;

    [[nodiscard]] std::uint8_t urgency() const noexcept
    {
        return priority > 7 ? std::uint8_t{7} : priority;
    }
    std::string url;
    std::vector<Header> headers;
    Body body;
    Version version = Version::negotiate;
    bool followRedirects = true;

    /// True when the request carries a body worth rewriting on a 303.
    [[nodiscard]] bool hasBody() const noexcept { return !body.empty(); }

    /// Append a UTF-8 payload as wire bytes.
    void setBody(std::string_view text);
};

} // namespace antares::domain::http
