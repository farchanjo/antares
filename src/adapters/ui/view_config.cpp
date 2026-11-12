#include "adapters/ui/view_config.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace antares::adapters::ui {

ViewConfig::ViewConfig() noexcept : columns_(defaultLayout) {}

ViewConfig::ViewConfig(std::span<const ColumnWidth> savedLayout) noexcept : columns_(defaultLayout)
{
    if (!coversEveryRole(savedLayout)) {
        return;
    }
    // Applied by role, not by position: a saved layout written before a family
    // gained a column still restores the widths it does name. A zero width or a
    // duplicate is ignored.
    std::array<bool, ROW_ROLE_COUNT> seen{};
    for (const ColumnWidth &column : savedLayout) {
        const auto slot = static_cast<std::size_t>(column.role);
        if (seen[slot] || column.width == 0) {
            continue;
        }
        seen[slot] = true;
        columns_[slot] = column;
    }
}

bool ViewConfig::coversEveryRole(std::span<const ColumnWidth> layout) noexcept
{
    // The four base roles are the contract: a saved layout that does not name
    // all of them came from a build this one cannot read, and the defaults win.
    std::array<bool, ROW_ROLE_COUNT> seen{};
    for (const ColumnWidth &column : layout) {
        const auto slot = static_cast<std::size_t>(column.role);
        if (slot >= ROW_ROLE_COUNT) {
            return false;
        }
        seen[slot] = true;
    }
    return seen[static_cast<std::size_t>(RowRole::name)] &&
           seen[static_cast<std::size_t>(RowRole::namespaceName)] &&
           seen[static_cast<std::size_t>(RowRole::status)] &&
           seen[static_cast<std::size_t>(RowRole::age)];
}

void ViewConfig::noteRowHeight(std::uint32_t height) noexcept
{
    if (!rowHeight_) {
        rowHeight_ = height;
        return;
    }
    if (*rowHeight_ != height) {
        uniform_ = false;
    }
}

} // namespace antares::adapters::ui
