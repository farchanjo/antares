#pragma once

#include <string_view>

namespace antares::domain::k8s {

/// One label pair, borrowed from the `Labels` blob it was read out of. It is a
/// view on purpose: the store keeps the label text once, in one string.
struct Label {
    std::string_view key;
    std::string_view value;
};

} // namespace antares::domain::k8s
