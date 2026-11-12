#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace antares::domain::json {

/// Why the selective scan refused a document.
///
/// Skipping is not trusting (FR-003): every bracket, quote and escape the walk
/// crosses is checked, and the first mismatch is reported with its byte offset
/// and one rule. The error never carries the bytes, so a refusal is cheap and a
/// payload does not leak into a log line.
///
/// The last four reasons are not shape failures. They name a document the
/// scanner read correctly and the projection then refused, which is the other
/// half of the contract: a bad `uid` is not malformed JSON, it is a refused
/// object and the caller maps it to `StoreError`.
enum class ScanReason : std::uint8_t {
    /// The document ended inside a value or inside a skip.
    unexpectedEnd,
    /// A byte that cannot start or continue a value.
    unexpectedCharacter,
    /// A raw control byte inside a string; JSON requires it escaped.
    controlCharacter,
    /// A backslash escape outside `" \\ / b f n r t u`.
    badEscape,
    /// `\u` not followed by four hexadecimal digits.
    badUnicodeEscape,
    /// A UTF-16 surrogate without its partner.
    loneSurrogate,
    /// A string that never closes.
    unterminatedString,
    /// A `,` immediately before the closing delimiter.
    trailingComma,
    /// A member name where the grammar requires one.
    expectedMemberName,
    /// A member name not followed by `:`.
    expectedColon,
    /// Neither `,` nor the closing delimiter after an entry.
    expectedSeparator,
    /// The nesting bound was reached.
    depthCeiling,
    /// Bytes after the one document value.
    trailingContent,
    /// No `metadata.name` in the document (FR-005).
    missingName,
    /// `metadata.uid` is absent or is not a UUID.
    refusedUid,
    /// `metadata.name` is empty, too long, or carries `/` or `%`.
    refusedName,
    /// `kind` is empty or too long.
    refusedKind,
    /// `metadata.resourceVersion` is too long.
    refusedVersion,
};

/// A refusal, with the byte offset it happened at.
struct ScanError {
    ScanReason reason = ScanReason::unexpectedCharacter;
    std::size_t offset = 0;
};

/// The rule name, for a case that asserts a reason and for a log line.
[[nodiscard]] std::string_view scanReasonName(ScanReason reason) noexcept;

} // namespace antares::domain::json
