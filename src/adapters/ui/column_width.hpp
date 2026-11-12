#pragma once

#include <cstdint>

#include "adapters/ui/row_role.hpp"

namespace antares::adapters::ui {

/// How wide one column is, in pixels, and which role it shows (FR-007).
///
/// Widths are `Fixed`: the header never measures contents, because measuring a
/// scope above a thousand rows costs a walk per resize and `ResizeToContents` is
/// the classic way a large list stalls on open.
struct ColumnWidth {
    RowRole role = RowRole::name;
    std::uint32_t width = 0;
};

} // namespace antares::adapters::ui
