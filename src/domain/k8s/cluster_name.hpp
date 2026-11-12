#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

#include "domain/k8s/store_error.hpp"

namespace antares::domain::k8s {

/// The name a kubeconfig gave a cluster. It is the outer part of every scope,
/// so it is bounded like the rest of the key.
class ClusterName {
public:
    static constexpr std::size_t ceiling = 64;

    ClusterName() = default;

    [[nodiscard]] static std::expected<ClusterName, StoreError> of(std::string_view text);

    [[nodiscard]] const std::string &text() const noexcept { return text_; }

    friend bool operator==(const ClusterName &, const ClusterName &) noexcept = default;

private:
    std::string text_;
};

} // namespace antares::domain::k8s
