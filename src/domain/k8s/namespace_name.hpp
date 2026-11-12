#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

#include "domain/k8s/store_error.hpp"

namespace antares::domain::k8s {

/// The namespace a scope is bound to. Empty means cluster-scoped, which is why
/// the type refuses anything but a DNS-1123 label rather than treating absent
/// and malformed alike.
class NamespaceName {
public:
    static constexpr std::size_t ceiling = 63;

    NamespaceName() = default;

    /// Empty is accepted and stays cluster-scoped.
    [[nodiscard]] static std::expected<NamespaceName, StoreError> of(std::string_view text);

    [[nodiscard]] const std::string &text() const noexcept { return text_; }
    [[nodiscard]] bool empty() const noexcept { return text_.empty(); }

    friend bool operator==(const NamespaceName &, const NamespaceName &) noexcept = default;

private:
    std::string text_;
};

} // namespace antares::domain::k8s
