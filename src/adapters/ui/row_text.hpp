#pragma once

#include <string>

#include "adapters/ui/row_role.hpp"
#include "domain/k8s/entry.hpp"
#include "domain/k8s/scope.hpp"

namespace antares::adapters::ui {

/// The text one role shows for one stored entry.
///
/// The namespace role reads the scope, not the entry: the entry does not repeat
/// it (FR-003 of feature 005), so the model has to hand its own scope in.
///
/// The age role is the entry's `creationTimestamp` verbatim. Turning that into
/// "5m" needs a clock, and a clock in the model would make every case
/// time-dependent; the delegate formats it instead.
[[nodiscard]] std::string rowText(const domain::k8s::Entry &entry,
                                  const domain::k8s::Scope &scope, RowRole role);

} // namespace antares::adapters::ui
