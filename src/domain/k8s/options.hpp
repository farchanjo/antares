#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "domain/k8s/api_path.hpp"

namespace antares::domain::k8s {

/// How a partial body is described to the server (FR-003). The value carries
/// its own wire content type, so a caller cannot pair the two wrongly.
enum class PatchType : std::uint8_t { merge, strategic, apply };

[[nodiscard]] std::string_view patchContentType(PatchType type) noexcept;

/// What a delete does with the objects that depend on the target (FR-003).
enum class Propagation : std::uint8_t { background, foreground, orphan };

[[nodiscard]] std::string_view propagationName(Propagation policy) noexcept;

/// The paging and filtering parameters of a list (FR-002).
///
/// Every field is optional, and an unset field is absent from the query rather
/// than empty — the server reads `limit=0` as "no limit" and `limit=` as a
/// malformed request. The continuation token round-trips untouched.
struct ListOptions {
    Selector label;
    Selector field;
    std::optional<std::uint32_t> limit;
    std::optional<std::string> continueToken;
    bool watch = false;
    std::optional<std::string> resourceVersion;
    std::optional<std::uint32_t> timeoutSeconds;
    /// Which container a `/log` stream addresses. The API server refuses a pod
    /// with more than one and no name, so it is not optional in practice.
    std::optional<std::string> container;
    bool allowWatchBookmarks = false;

    /// The query string without the leading `?`; empty when nothing is set.
    [[nodiscard]] std::string render() const;
};

/// The body of a delete, when the caller has one.
struct DeleteOptions {
    Propagation propagation = Propagation::background;
    std::optional<std::string> resourceVersion;

    [[nodiscard]] std::string render() const;
};

/// Percent-encode a query value (RFC 3986 §2.1). Space becomes `%20`, never `+`.
[[nodiscard]] std::string percentEncode(std::string_view text);

} // namespace antares::domain::k8s
