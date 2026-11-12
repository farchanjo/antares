#pragma once

#include <expected>

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

} // namespace antares::domain::k8s
