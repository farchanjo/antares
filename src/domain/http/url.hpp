#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "domain/http/http_error.hpp"

namespace antares::domain::http {

enum class Scheme : std::uint8_t { http, https };

/// A parsed absolute URL. The only place a URL is taken apart (FR-006 needs the
/// authority to compare origins, the connection needs host and port).
struct Url {
    Scheme scheme = Scheme::https;
    std::string host;
    std::uint16_t port = 443;
    std::string target = "/";
    std::string authority;

    [[nodiscard]] bool isTls() const noexcept { return scheme == Scheme::https; }

    /// True when scheme, host and port all match.
    [[nodiscard]] bool sameOrigin(const Url &other) const noexcept;

    [[nodiscard]] static std::expected<Url, HttpError> parse(std::string_view text);
};

} // namespace antares::domain::http
