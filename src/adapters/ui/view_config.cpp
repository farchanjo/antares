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
    for (std::size_t at = 0; at < savedLayout.size(); ++at) {
        columns_[at] = savedLayout[at];
    }
}

bool ViewConfig::coversEveryRole(std::span<const ColumnWidth> layout) noexcept
{
    if (layout.size() != ROW_ROLE_COUNT) {
        return false;
    }
    std::array<bool, ROW_ROLE_COUNT> seen{};
    for (const ColumnWidth &column : layout) {
        const auto slot = static_cast<std::size_t>(column.role);
        if (seen[slot] || column.width == 0) {
            return false;
        }
        seen[slot] = true;
    }
    return std::all_of(seen.begin(), seen.end(), [](bool seenRole) { return seenRole; });
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
