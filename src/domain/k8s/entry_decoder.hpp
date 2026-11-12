#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

#include "domain/json/scalar_scan.hpp"
#include "domain/json/value.hpp"
#include "domain/k8s/entry.hpp"
#include "domain/k8s/store_error.hpp"

namespace antares::domain::k8s {

/// Read the projection of one decoded object.
///
/// Only the members below are touched. `managedFields`, `annotations` and a
/// `Secret`'s `data`/`stringData` are never read, so they are dropped with the
/// document (FR-002). The overloads return the projection itself — never a
/// `json::Value` — so no payload subtree is copied out (FR-005).
[[nodiscard]] std::expected<Entry, StoreError> decodeEntry(const json::Value &object);

/// Read the projection straight out of the bytes, allocating only for the
/// fields the entry keeps (FR-002). A value outside the projection is validated
/// and stepped over, never built.
[[nodiscard]] std::expected<Entry, json::ScanError> scanEntry(json::ScalarScan &scan);

/// The same over one complete document, which must end after the value.
[[nodiscard]] std::expected<Entry, json::ScanError> scanEntry(std::string_view document);

/// The name a resource is addressed by, found by walking `metadata.name` and
/// stopping there (FR-005). `consumed` receives the bytes the walk crossed, so
/// a case can prove it stopped instead of reading the frame.
[[nodiscard]] std::expected<std::string, json::ScanError>
scanName(std::string_view document, std::size_t *consumed = nullptr);

/// Scan the document and fall back to `Value::parse` for a shape the scanner
/// refuses (FR-001). `scannerRefused` is set when the fallback ran, so a
/// document the parser accepts and the scanner does not is visible (T010).
[[nodiscard]] std::expected<Entry, StoreError> decodeEntry(std::string_view document,
                                                           bool *scannerRefused = nullptr);

/// The same one value at a time, for a caller already walking a document (a
/// list page). The cursor is left after the value.
[[nodiscard]] std::expected<Entry, StoreError> decodeEntry(json::ScalarScan &cursor,
                                                           bool *scannerRefused = nullptr);

} // namespace antares::domain::k8s
