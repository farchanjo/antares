#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "domain/k8s/entry.hpp"

namespace antares::domain::k8s {

/// The four entry fields a repaint reads. Closed on purpose: the mask is what a
/// list model turns into roles, and a new field must be a compile error at the
/// one place that maps it (FR-008).
enum class EntryField : std::uint8_t { resourceVersion, generation, labels, status };

[[nodiscard]] std::string_view entryFieldName(EntryField field) noexcept;

/// A half-open row range: `first` up to and excluding `last`.
struct RowRange {
    std::uint32_t first = 0;
    std::uint32_t last = 0;
};

/// One changed row and the fields of it that moved.
struct ChangedRow {
    std::uint32_t row = 0;
    std::vector<EntryField> fields;
};

/// One batch in row terms, ready for `beginRemoveRows`, `beginInsertRows` and
/// `dataChanged` (FR-008).
///
/// `removed` is stated against the rows before the batch and is sorted
/// descending, `inserted` against the rows after it and sorted ascending, so a
/// mirror can replay both without renumbering. Replaying a change set onto a
/// mirror of the rows reproduces the store's row order exactly.
struct ChangeSet {
    std::vector<RowRange> removed;
    std::vector<RowRange> inserted;
    std::vector<ChangedRow> changed;

    [[nodiscard]] bool empty() const noexcept
    {
        return removed.empty() && inserted.empty() && changed.empty();
    }

    /// Which of the four rendered fields differ. An empty mask means the delta
    /// touched nothing a view shows, so no row is repainted.
    [[nodiscard]] static std::vector<EntryField> diff(const Entry &before, const Entry &after);
};

} // namespace antares::domain::k8s
