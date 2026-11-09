#pragma once

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

/// One submission. `version` overrides ALPN and is test-only (FR-005).
struct Request {
    Method method = Method::get;
    std::string url;
    std::vector<Header> headers;
    std::string body;
    Version version = Version::negotiate;
    bool followRedirects = true;

    /// True when the request carries a body worth rewriting on a 303.
    [[nodiscard]] bool hasBody() const noexcept { return !body.empty(); }
};

} // namespace antares::domain::http
