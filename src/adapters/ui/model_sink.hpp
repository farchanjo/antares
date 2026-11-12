#pragma once

#include <cstdint>
#include <span>

#include "adapters/ui/row_role.hpp"

namespace antares::adapters::ui {

/// What a model tells its view, in the vocabulary `QAbstractItemModel` uses.
///
/// The model speaks this instead of Qt because the toolkit is not linkable in
/// every checkout: `Qt6Core` here is built against libstdc++ while the tree
/// links libc++, so a `QString`-returning call is an undefined symbol. The
/// interface is the whole of the crossing, and `QtListModel` is the only class
/// that has to know Qt exists.
///
/// The store is already in its post-batch state when a call arrives: the batch
/// is applied by `ObjectStore::apply`, which publishes afterwards, so a model
/// cannot bracket the mutation itself. The adapter still wraps each call in
/// `begin*`/`end*`; Qt carries the indices as arguments and does not re-derive
/// them, so an already-applied batch is the documented case, not a tolerated
/// accident.
class ModelSink {
public:
    ModelSink() = default;
    ModelSink(const ModelSink &) = delete;
    ModelSink &operator=(const ModelSink &) = delete;
    ModelSink(ModelSink &&) = delete;
    ModelSink &operator=(ModelSink &&) = delete;
    virtual ~ModelSink() = default;

    /// `[first, last]` arrived.
    /// Everything changed at once: a filter, a refresh, a new scope.
    virtual void rowsReset() = 0;

    virtual void rowsInserted(std::uint32_t first, std::uint32_t last) = 0;

    /// `[first, last]` left.
    virtual void rowsRemoved(std::uint32_t first, std::uint32_t last) = 0;

    /// A row was re-sorted. `destinationChild` follows the documented rule:
    /// moving down it is the target plus one, moving up it is the target.
    virtual void rowsMoved(std::uint32_t first, std::uint32_t last,
                           std::uint32_t destinationChild) = 0;

    /// `[first, last]` changed, for the listed roles only.
    virtual void dataChanged(std::uint32_t first, std::uint32_t last,
                             std::span<const RowRole> roles) = 0;
};

} // namespace antares::adapters::ui
