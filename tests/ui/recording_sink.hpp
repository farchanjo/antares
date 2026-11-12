#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "adapters/ui/model_sink.hpp"
#include "adapters/ui/row_role.hpp"

namespace antares::test::ui {

using adapters::ui::RowRole;

/// One recorded model notification, with the index arguments it carried.
struct RecordedOp {
    enum class Kind : std::uint8_t { inserted, removed, moved, changed };

    Kind kind = Kind::inserted;
    std::uint32_t first = 0;
    std::uint32_t last = 0;
    std::uint32_t destination = 0;
    std::vector<RowRole> roles;

    friend bool operator==(const RecordedOp &op, Kind kind) noexcept { return op.kind == kind; }
};

/// A sink that keeps every operation, so a case can assert the order Qt
/// requires instead of trusting it.
class RecordingSink : public adapters::ui::ModelSink {
public:
    std::size_t resets = 0;

    void rowsReset() override { ++resets; }

    void rowsInserted(std::uint32_t first, std::uint32_t last) override
    {
        record(RecordedOp::Kind::inserted, first, last);
    }

    void rowsRemoved(std::uint32_t first, std::uint32_t last) override
    {
        record(RecordedOp::Kind::removed, first, last);
    }

    void rowsMoved(std::uint32_t first, std::uint32_t last,
                   std::uint32_t destinationChild) override
    {
        RecordedOp op;
        op.kind = RecordedOp::Kind::moved;
        op.first = first;
        op.last = last;
        op.destination = destinationChild;
        ops.push_back(std::move(op));
    }

    void dataChanged(std::uint32_t first, std::uint32_t last,
                     std::span<const RowRole> roles) override
    {
        RecordedOp op;
        op.kind = RecordedOp::Kind::changed;
        op.first = first;
        op.last = last;
        op.roles.assign(roles.begin(), roles.end());
        ops.push_back(std::move(op));
    }

    [[nodiscard]] std::size_t count(RecordedOp::Kind kind) const
    {
        std::size_t total = 0;
        for (const RecordedOp &op : ops) {
            if (op.kind == kind) {
                ++total;
            }
        }
        return total;
    }

    void clear() { ops.clear(); }

    std::vector<RecordedOp> ops;

private:
    void record(RecordedOp::Kind kind, std::uint32_t first, std::uint32_t last)
    {
        RecordedOp op;
        op.kind = kind;
        op.first = first;
        op.last = last;
        ops.push_back(std::move(op));
    }
};

} // namespace antares::test::ui
