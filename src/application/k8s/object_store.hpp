#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "domain/units/duration.hpp"

namespace antares::application::k8s {

/// One stored object, reduced to what a list row renders (FR-002 of 005).
///
/// The full document is not kept: a list row reads four fields, and retaining
/// the rest costs an order of magnitude per object for nothing. Feature 005
/// replaces this skeleton with the keyed, admission-bounded store; the row shape
/// and the read-only accessors are the part this feature depends on, so they are
/// already the ones it will keep.
struct Entry {
    std::string uid;
    std::string name;
    std::string namespaceName;
    std::string status;
    domain::Duration age{};
};

/// The rows of one scope, in arrival order.
///
/// The store owns the data and the model holds none of it: a view resolves a row
/// through `at()` on every `data()` call. The store is not a `QObject`, so it
/// carries no signal and does not know about Qt.
class ObjectStore {
public:
    /// Replace every row. The initial load and a relist both go through here,
    /// so a scope has one shape of update rather than two.
    void replace(std::vector<Entry> entries);

    /// Append one row at the end.
    void append(Entry entry);

    void clear();

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    /// The row at `index`. The caller guarantees `index < size()`.
    [[nodiscard]] const Entry &at(std::size_t index) const;

    /// Bumped on every change, so a view can tell a stale read from a fresh one.
    [[nodiscard]] std::uint64_t revision() const noexcept;

private:
    std::vector<Entry> rows_;
    std::uint64_t revision_ = 0;
};

} // namespace antares::application::k8s
