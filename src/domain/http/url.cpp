#include "domain/http/url.hpp"

#include <charconv>

namespace antares::domain::http {
namespace {

constexpr std::uint16_t DEFAULT_HTTP_PORT = 80;
constexpr std::uint16_t DEFAULT_HTTPS_PORT = 443;

bool startsWith(std::string_view text, std::string_view prefix) noexcept
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

} // namespace

bool Url::sameOrigin(const Url &other) const noexcept
{
    return scheme == other.scheme && host == other.host && port == other.port;
}

std::expected<Url, HttpError> Url::parse(std::string_view text)
{
    Url url;

    if (startsWith(text, "https://")) {
        url.scheme = Scheme::https;
        url.port = DEFAULT_HTTPS_PORT;
        text.remove_prefix(8);
    } else if (startsWith(text, "http://")) {
        url.scheme = Scheme::http;
        url.port = DEFAULT_HTTP_PORT;
        text.remove_prefix(7);
    } else {
        return std::unexpected(HttpError::connect);
    }

    const std::size_t slash = text.find('/');
    const std::string_view authority = text.substr(0, slash);
    if (authority.empty()) {
        return std::unexpected(HttpError::connect);
    }

    const std::size_t colon = authority.rfind(':');
    if (colon != std::string_view::npos) {
        const std::string_view digits = authority.substr(colon + 1);
        std::uint32_t port = 0;
        const auto *begin = digits.data();
        const auto *end = begin + digits.size();
        if (std::from_chars(begin, end, port).ec != std::errc{} || port == 0 || port > 65535) {
            return std::unexpected(HttpError::connect);
        }
        url.port = static_cast<std::uint16_t>(port);
        url.host = std::string{authority.substr(0, colon)};
    } else {
        url.host = std::string{authority};
    }

    if (url.host.empty()) {
        return std::unexpected(HttpError::connect);
    }

    url.target = slash == std::string_view::npos ? "/" : std::string{text.substr(slash)};
    url.authority = url.port == (url.isTls() ? DEFAULT_HTTPS_PORT : DEFAULT_HTTP_PORT)
                        ? url.host
                        : url.host + ":" + std::to_string(url.port);
    return url;
}

} // namespace antares::domain::http
