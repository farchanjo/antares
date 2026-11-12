#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace antares::domain::k8s {

/// The bounded part of an object's `status` a view renders.
///
/// `status` is the second-largest member of most objects and almost none of it
/// is shown, so only the reason and a ready/total pair are kept; both numbers
/// are optional because most kinds carry neither (FR-002).
struct StatusSummary {
    static constexpr std::size_t reasonCeiling = 128;

    std::string reason;
    std::optional<std::uint32_t> ready;
    std::optional<std::uint32_t> total;

    [[nodiscard]] bool empty() const noexcept { return reason.empty() && !ready && !total; }

    friend bool operator==(const StatusSummary &, const StatusSummary &) noexcept = default;
};

} // namespace antares::domain::k8s
