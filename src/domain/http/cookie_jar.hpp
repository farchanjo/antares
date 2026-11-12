#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace antares::domain::http {

/// What the jar learned from one `Set-Cookie`.
struct Cookie {
    std::string name;
    std::string value;
    std::string domain;
    std::string path;
    bool secure = false;
    bool hostOnly = true;
    bool persistent = false;
    std::chrono::steady_clock::time_point created{};
    std::optional<std::chrono::steady_clock::time_point> expires{};
};

/// One cookie store per client. The rules are RFC 6265 §5.2-5.4, without the
/// public-suffix list: a `Domain` that is a suffix of the request host is
/// accepted, which is what §5.2.3 asks for and what a client without the PSL
/// can honestly do.
class CookieJar {
public:
    /// Store what `header` sets, in the context of a request to `host` + `path`
    /// over `secure`. A refused header leaves the jar untouched.
    void store(std::string_view header, std::string_view host, std::string_view path, bool secure);

    /// The `Cookie` header for this request, or nothing when no cookie applies.
    /// Ordered longest path first, then oldest, as §5.4 wants.
    [[nodiscard]] std::string headerFor(std::string_view host, std::string_view path,
                                        bool secure);

    /// Drop what has expired.
    void prune(std::chrono::steady_clock::time_point now);

    [[nodiscard]] std::size_t size() const noexcept { return cookies_.size(); }
    void clear() noexcept { cookies_.clear(); }

private:
    std::vector<Cookie> cookies_;
};

/// §5.1.3: the host is the domain, or the domain is a dot-suffix of it.
[[nodiscard]] bool domainMatches(std::string_view host, std::string_view domain) noexcept;

/// §5.1.4: the request path is the cookie path, or the cookie path is a prefix
/// of it ending at a `/`.
[[nodiscard]] bool pathMatches(std::string_view requestPath, std::string_view cookiePath) noexcept;

/// §5.1.4: the default path is the request path up to its last `/`.
[[nodiscard]] std::string defaultPath(std::string_view requestPath);

} // namespace antares::domain::http
