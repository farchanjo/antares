#pragma once

#include <cstdint>
#include <string>

#include "adapters/ui/page_source.hpp"
#include "domain/k8s/change_set.hpp"

namespace antares::adapters::ui {

/// One unit of GUI-thread work: a batch to apply, or the settlement of a page
/// request.
///
/// Both ride one queue so their order is the order they were produced in. A
/// page's rows are therefore always inserted before its token is stored, and the
/// `continue` token can never name a page whose rows are still pending.
struct Queued {
    enum class Kind : std::uint8_t { changes, page };

    Kind kind = Kind::changes;
    domain::k8s::ChangeSet changes;
    PageOutcome page;
};

} // namespace antares::adapters::ui
