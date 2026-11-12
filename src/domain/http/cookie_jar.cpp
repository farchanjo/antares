#include "domain/http/cookie_jar.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace antares::domain::http {
namespace {

[[nodiscard]] std::string_view trim(std::string_view text) noexcept
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] std::string lower(std::string_view text)
{
    std::string out{text};
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

[[nodiscard]] bool equalsIgnoringCase(std::string_view left, std::string_view right) noexcept
{
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           });
}

/// §5.2.3: a leading dot is ignored, and the value must not be an IP.
[[nodiscard]] bool usableDomain(std::string_view value) noexcept
{
    if (value.empty()) {
        return false;
    }
    // An IPv4 literal or a bracketed IPv6 is host-only by definition.
    return value.find('.') != std::string_view::npos && value.find(':') == std::string_view::npos;
}

} // namespace

bool domainMatches(std::string_view host, std::string_view domain) noexcept
{
    if (host.size() == domain.size()) {
        return equalsIgnoringCase(host, domain);
    }
    if (host.size() < domain.size()) {
        return false;
    }
    // `*.example.com` matches `example.com` only through the dot.
    const std::size_t offset = host.size() - domain.size();
    return host[offset - 1] == '.' && equalsIgnoringCase(host.substr(offset), domain);
}

bool pathMatches(std::string_view requestPath, std::string_view cookiePath) noexcept
{
    if (cookiePath.empty()) {
        return true;
    }
    if (requestPath == cookiePath) {
        return true;
    }
    if (requestPath.size() < cookiePath.size()) {
        return false;
    }
    if (requestPath.compare(0, cookiePath.size(), cookiePath) != 0) {
        return false;
    }
    // A prefix only matches on a `/` boundary: `/foo` covers `/foo/bar`, not
    // `/foobar`.
    return cookiePath.back() == '/' || requestPath[cookiePath.size()] == '/';
}

std::string defaultPath(std::string_view requestPath)
{
    if (requestPath.empty() || requestPath.front() != '/') {
        return "/";
    }
    const std::size_t slash = requestPath.rfind('/');
    if (slash == 0 || slash == std::string_view::npos) {
        return "/";
    }
    return std::string{requestPath.substr(0, slash)};
}

void CookieJar::store(std::string_view header, std::string_view host, std::string_view path,
                      bool secure)
{
    const std::size_t semicolon = header.find(';');
    const std::string_view pair = trim(header.substr(0, semicolon));

    const std::size_t equals = pair.find('=');
    if (equals == std::string_view::npos || equals == 0) {
        return;
    }

    Cookie cookie;
    cookie.name = std::string{pair.substr(0, equals)};
    cookie.value = std::string{pair.substr(equals + 1)};
    cookie.domain = lower(host);
    cookie.path = defaultPath(path);
    cookie.created = std::chrono::steady_clock::now();

    bool expiredNow = false;
    std::optional<std::chrono::seconds> maxAge;

    std::size_t start = semicolon == std::string_view::npos ? header.size() : semicolon + 1;
    while (start < header.size()) {
        const std::size_t next = header.find(';', start);
        const std::string_view attribute =
            trim(header.substr(start, next == std::string_view::npos ? std::string_view::npos
                                                                     : next - start));
        const std::size_t assign = attribute.find('=');
        const std::string_view key = trim(attribute.substr(0, assign));
        const std::string_view value =
            assign == std::string_view::npos ? std::string_view{} : trim(attribute.substr(assign + 1));

        if (equalsIgnoringCase(key, "secure")) {
            cookie.secure = true;
        } else if (equalsIgnoringCase(key, "max-age")) {
            std::int64_t seconds = 0;
            const auto parsed =
                std::from_chars(value.data(), value.data() + value.size(), seconds);
            if (parsed.ec == std::errc{} && value.data() != parsed.ptr) {
                maxAge = std::chrono::seconds{seconds};
            }
        } else if (equalsIgnoringCase(key, "domain") && usableDomain(value)) {
            const std::string_view domain = value.front() == '.' ? value.substr(1) : value;
            cookie.domain = lower(domain);
            cookie.hostOnly = false;
        } else if (equalsIgnoringCase(key, "path")) {
            if (!value.empty() && value.front() == '/') {
                cookie.path = std::string{value};
            }
        }

        if (next == std::string_view::npos) {
            break;
        }
        start = next + 1;
    }

    // §5.3: a Domain that does not cover the request host is refused outright,
    // and a Secure cookie is not accepted from a clear channel.
    if (!cookie.hostOnly && !domainMatches(host, cookie.domain)) {
        return;
    }
    if (cookie.secure && !secure) {
        return;
    }

    if (maxAge.has_value()) {
        cookie.persistent = true;
        cookie.expires = cookie.created + *maxAge;
        expiredNow = maxAge->count() <= 0;
    }

    // §5.3: same name, domain and path evicts the old one.
    const auto existing = std::find_if(
        cookies_.begin(), cookies_.end(), [&cookie](const Cookie &known) {
            return known.name == cookie.name && known.domain == cookie.domain &&
                   known.path == cookie.path;
        });
    if (existing != cookies_.end()) {
        cookies_.erase(existing);
    }
    if (!expiredNow) {
        cookies_.push_back(std::move(cookie));
    }
}

std::string CookieJar::headerFor(std::string_view host, std::string_view path, bool secure)
{
    prune(std::chrono::steady_clock::now());

    std::vector<const Cookie *> matching;
    for (const Cookie &cookie : cookies_) {
        if (cookie.secure && !secure) {
            continue;
        }
        if (cookie.hostOnly ? !equalsIgnoringCase(host, cookie.domain)
                            : !domainMatches(host, cookie.domain)) {
            continue;
        }
        if (!pathMatches(path, cookie.path)) {
            continue;
        }
        matching.push_back(&cookie);
    }

    std::sort(matching.begin(), matching.end(), [](const Cookie *left, const Cookie *right) {
        if (left->path.size() != right->path.size()) {
            return left->path.size() > right->path.size();
        }
        return left->created < right->created;
    });

    std::string header;
    for (const Cookie *cookie : matching) {
        if (!header.empty()) {
            header += "; ";
        }
        header += cookie->name;
        header += '=';
        header += cookie->value;
    }
    return header;
}

void CookieJar::prune(std::chrono::steady_clock::time_point now)
{
    std::erase_if(cookies_, [now](const Cookie &cookie) {
        return cookie.expires.has_value() && *cookie.expires <= now;
    });
}

} // namespace antares::domain::http
