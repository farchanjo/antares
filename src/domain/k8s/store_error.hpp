#pragma once

#include <cstdint>
#include <string_view>

namespace antares::domain::k8s {

/// Why the store or its decoder refused something. A refusal is never a
/// throw: a hostile object is one bad row, not a lost scope (FR-003, FR-004).
enum class StoreError : std::uint8_t {
    /// `metadata.uid` is absent or is not a UUID.
    invalidUid,
    /// `metadata.name` is empty, longer than 253 bytes, or carries `/` or `%`.
    invalidName,
    /// The namespace is not a DNS-1123 label, or is longer than 63 bytes.
    invalidNamespace,
    /// The cluster name is empty or longer than 64 bytes.
    invalidCluster,
    /// `kind` is empty or longer than 64 bytes.
    invalidKind,
    /// `resourceVersion` is longer than 64 bytes.
    invalidVersion,
    /// The store already holds `maxScopes` scopes (FR-004).
    tooManyScopes,
    /// No such scope is open.
    unknownScope,
    /// No row carries the uid.
    unknownEntry,
    /// A relist page arrived without an open epoch, or for a stale one.
    notRelisting,
    /// Neither the scanner nor the parser could read the document (FR-001).
    malformedDocument,
};

[[nodiscard]] std::string_view storeErrorName(StoreError error) noexcept;

} // namespace antares::domain::k8s
