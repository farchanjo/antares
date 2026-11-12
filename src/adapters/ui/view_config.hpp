#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "adapters/ui/column_width.hpp"
#include "adapters/ui/row_role.hpp"

namespace antares::adapters::ui {

/// The fixed part of the list view (FR-006, FR-007).
///
/// Layout is `Batched`, so a burst of inserts is laid out once per batch instead
/// of per row, and `uniformItemSizes` is on. Uniformity is conditional: Qt
/// documents the hint as "every item is the same size", and a row that varies
/// makes it a lie and the scroll arithmetic wrong, so the first height that
/// differs clears it for good (T007).
///
/// A saved layout wins over the defaults when it names every role exactly once
/// with a positive width; anything else falls back to the defaults instead of
/// rendering a zero-width column.
class ViewConfig {
public:
    static constexpr std::uint32_t defaultBatchSize = 100;

    static constexpr std::array<ColumnWidth, ROW_ROLE_COUNT> defaultLayout{
        ColumnWidth{RowRole::name, 320},
        ColumnWidth{RowRole::namespaceName, 160},
        ColumnWidth{RowRole::status, 140},
        ColumnWidth{RowRole::age, 120},
    };

    ViewConfig() noexcept;

    /// Build from a saved layout. A layout that does not cover every role once
    /// is ignored and the defaults are kept.
    explicit ViewConfig(std::span<const ColumnWidth> savedLayout) noexcept;

    [[nodiscard]] bool batched() const noexcept { return true; }
    [[nodiscard]] std::uint32_t batchSize() const noexcept { return defaultBatchSize; }
    [[nodiscard]] bool uniformItemSizes() const noexcept { return uniform_; }
    [[nodiscard]] bool headerResizeFixed() const noexcept { return true; }
    [[nodiscard]] std::span<const ColumnWidth> columns() const noexcept { return columns_; }

    /// Record the height a row just measured. One height is remembered, not one
    /// per row, so this adds no per-row storage.
    void noteRowHeight(std::uint32_t height) noexcept;

private:
    [[nodiscard]] static bool coversEveryRole(std::span<const ColumnWidth> layout) noexcept;

    std::array<ColumnWidth, ROW_ROLE_COUNT> columns_;
    std::optional<std::uint32_t> rowHeight_;
    bool uniform_ = true;
};

} // namespace antares::adapters::ui
