#pragma once

#include "domain/k8s/scope.hpp"
#include "domain/k8s/uid.hpp"

namespace antares::domain::k8s {

/// The store's only index: a scope and a uid (FR-001).
///
/// Two views that resolve the same key are handed the same stored entry, which
/// is what makes the second view free.
struct ObjectKey {
    Scope scope;
    Uid uid;

    friend bool operator==(const ObjectKey &, const ObjectKey &) noexcept = default;
};

} // namespace antares::domain::k8s
