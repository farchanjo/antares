#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include "domain/k8s/store_error.hpp"

namespace antares::domain::k8s {

/// An object's `metadata.name`: anything the server accepts, bounded.
///
/// Distinct from `NamespaceName` because the server's rules differ — a name may
/// carry dots and is allowed 253 bytes, a namespace is one DNS-1123 label of 63
/// (D-05). Only the ceiling and the two path hosts are checked here; a name the
/// server accepted must never be refused by us for a stricter rule.
class ObjectName {
public:
    static constexpr std::size_t ceiling = 253;

    ObjectName() = default;

    [[nodiscard]] static std::expected<ObjectName, StoreError> of(std::string_view text);

    [[nodiscard]] const std::string &text() const noexcept { return text_; }

    /// The text, moved out — the entry stores the name and does not need the
    /// wrapper, so taking it saves a second allocation.
    [[nodiscard]] std::string take() && noexcept { return std::move(text_); }
    [[nodiscard]] bool empty() const noexcept { return text_.empty(); }

    friend bool operator==(const ObjectName &, const ObjectName &) noexcept = default;

private:
    std::string text_;
};

} // namespace antares::domain::k8s
