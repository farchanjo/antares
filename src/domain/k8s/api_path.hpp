#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "domain/k8s/api_error.hpp"

namespace antares::domain::k8s {

/// A subresource the client knows how to address (FR-001).
enum class Subresource : std::uint8_t { none, status, scale, log, exec };

[[nodiscard]] std::string_view subresourceName(Subresource subresource) noexcept;

/// A label or field selector, rendered as one query parameter.
struct Selector {
    std::string text;

    [[nodiscard]] bool empty() const noexcept { return text.empty(); }
};

/// What identifies one REST resource: the group, the version and the plural
/// resource name. The core group is the empty string.
///
/// The group and version are validated as DNS-1123 subdomains and the resource
/// as a DNS-1123 label, so a hostile name cannot traverse the built path
/// (FR-001, Security: input validation). The object name is not part of this
/// descriptor and follows the wider `isPathSegment` rule instead.
struct GroupVersionResource {
    std::string group;
    std::string version;
    std::string resource;

    [[nodiscard]] bool isCore() const noexcept { return group.empty(); }

    /// Builds a descriptor, refusing a name outside DNS-1123.
    [[nodiscard]] static std::expected<GroupVersionResource, ApiError>
    of(std::string group, std::string version, std::string resource);
};

/// Where a resource lives and what is addressed, rendered as one path (FR-001).
struct ApiPath {
    GroupVersionResource type;
    std::optional<std::string> namespaceName;
    std::optional<std::string> name;
    Subresource subresource = Subresource::none;

    /// `/api/v1` for the core group, `/apis/<group>/<version>` otherwise.
    [[nodiscard]] std::string prefix() const;

    /// The full path from the API prefix to the subresource. A namespaced
    /// resource carries `/namespaces/<name>`, a named one the object name, and
    /// a subresource its own segment.
    [[nodiscard]] std::string render() const;

    /// Same value with a namespace attached. A namespace stays a DNS-1123
    /// label — the server does enforce that — so `isDnsLabel` guards this one
    /// and refuses uppercase, `_` and `:` (FR-002).
    [[nodiscard]] std::expected<ApiPath, ApiError> inNamespace(std::string name) const;

    /// Same value addressing one object. An object name is a path segment, not
    /// a DNS label, so `isPathSegment` guards this one and takes uppercase, `_`
    /// and `:` where `inNamespace` refuses them (FR-001). The two rules diverge
    /// on purpose; the server draws the same line.
    [[nodiscard]] std::expected<ApiPath, ApiError> named(std::string name) const;

    [[nodiscard]] ApiPath with(Subresource subresource) const;
};

/// The ceiling the server puts on a name, in bytes (FR-001, D-05).
inline constexpr std::size_t SEGMENT_CEILING = 253;

/// True when `text` is a DNS-1123 label: lowercase alphanumeric or `-`, at most
/// 63 characters, starting and ending alphanumeric.
///
/// This is the namespace rule (`inNamespace`) and the version and resource
/// rule. It is *not* the object-name rule — reach for `isPathSegment` there.
[[nodiscard]] bool isDnsLabel(std::string_view text) noexcept;

/// How a name fails the path-segment rule (FR-003). Closed on purpose: the
/// message stays bounded, the `debug` log line stays greppable, and a rule
/// added later is a compile error at every `switch`. The spellings match
/// `#RefusalReason` in `doc/arch/schemas/relax-api-path-name.cue`.
enum class NameRefusal : std::uint8_t { empty, tooLong, dot, dotdot, separator, escape };

/// The reason class as the token the refusal message carries.
[[nodiscard]] std::string_view refusalReason(NameRefusal refusal) noexcept;

/// The rule `text` breaks, or nothing when it is a legal segment (FR-003).
/// `isPathSegment` is this answer collapsed to a bool.
[[nodiscard]] std::optional<NameRefusal> segmentRefusal(std::string_view text) noexcept;

/// True when `text` is a path segment the server accepts as `metadata.name`:
/// 1 to 253 bytes, no `/` and no `%`, and neither `.` nor `..` (FR-001).
///
/// `%` is refused because the server decodes a path before it routes, so an
/// escape would arrive as one encoding too many; `.` and `..` are refused
/// because they resolve as segments (FR-004). Everything else passes, which is
/// what makes uppercase, `_` and `:` legal where a DNS label would refuse them.
[[nodiscard]] bool isPathSegment(std::string_view text) noexcept;

/// True when `text` is a DNS-1123 subdomain: dot-separated labels, at most 253
/// characters.
///
/// Only the group still uses this; `metadata.name` used to and no longer does,
/// so a reader who needs the name rule wants `isPathSegment`.
[[nodiscard]] bool isDnsSubdomain(std::string_view text) noexcept;

} // namespace antares::domain::k8s
