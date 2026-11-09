#include "domain/k8s/options.hpp"

#include <array>

namespace antares::domain::k8s {

namespace {

/// Characters left untouched in a query value. Everything else is escaped, so a
/// selector carrying `,`, `!` or `=` still survives the round trip.
[[nodiscard]] bool isUnreserved(char value) noexcept
{
    const bool alpha = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
    const bool digit = value >= '0' && value <= '9';
    const bool mark = value == '-' || value == '_' || value == '.' || value == '~';
    return alpha || digit || mark;
}

constexpr std::array<char, 16> hexDigits{'0', '1', '2', '3', '4',
                                         '5', '6', '7', '8', '9',
                                         'A', 'B', 'C', 'D', 'E', 'F'};

void appendPair(std::string &query, std::string_view key, std::string_view value)
{
    if (!query.empty()) {
        query += '&';
    }
    query += key;
    query += '=';
    query += percentEncode(value);
}

void appendNumber(std::string &query, std::string_view key, std::uint32_t value)
{
    appendPair(query, key, std::to_string(value));
}

} // namespace

std::string percentEncode(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char value : text) {
        if (isUnreserved(value)) {
            out += value;
            continue;
        }
        const auto byte = static_cast<unsigned char>(value);
        out += '%';
        out += hexDigits[byte >> 4U];
        out += hexDigits[byte & 0x0FU];
    }
    return out;
}

std::string_view patchContentType(PatchType type) noexcept
{
    switch (type) {
    case PatchType::merge:
        return "application/merge-patch+json";
    case PatchType::strategic:
        return "application/strategic-merge-patch+json";
    case PatchType::apply:
        return "application/apply-patch+yaml";
    }
    return "application/merge-patch+json";
}

std::string_view propagationName(Propagation policy) noexcept
{
    switch (policy) {
    case Propagation::background:
        return "Background";
    case Propagation::foreground:
        return "Foreground";
    case Propagation::orphan:
        return "Orphan";
    }
    return "Background";
}

std::string ListOptions::render() const
{
    std::string query;
    if (!label.empty()) {
        appendPair(query, "labelSelector", label.text);
    }
    if (!field.empty()) {
        appendPair(query, "fieldSelector", field.text);
    }
    if (limit) {
        appendNumber(query, "limit", *limit);
    }
    if (continueToken) {
        appendPair(query, "continue", *continueToken);
    }
    if (watch) {
        query += query.empty() ? "watch=true" : "&watch=true";
    }
    if (resourceVersion) {
        appendPair(query, "resourceVersion", *resourceVersion);
    }
    if (timeoutSeconds) {
        appendNumber(query, "timeoutSeconds", *timeoutSeconds);
    }
    if (allowWatchBookmarks) {
        query += query.empty() ? "allowWatchBookmarks=true" : "&allowWatchBookmarks=true";
    }
    return query;
}

std::string DeleteOptions::render() const
{
    std::string body = R"({"kind":"DeleteOptions","apiVersion":"v1","propagationPolicy":")";
    body += propagationName(propagation);
    body += '"';
    if (resourceVersion) {
        body += R"(,"preconditions":{"resourceVersion":")";
        body += *resourceVersion;
        body += R"("})";
    }
    body += '}';
    return body;
}

} // namespace antares::domain::k8s
