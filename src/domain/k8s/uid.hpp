#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "domain/k8s/store_error.hpp"

namespace antares::domain::k8s {

/// An object's `metadata.uid`, kept as the sixteen bytes a UUID names instead of
/// the thirty-six characters it is written as.
///
/// The uid is the store's only key and every byte of an entry is paid once per
/// stored object (FR-003), so the compact form is the stored form; `text()`
/// renders it back for the one caller that needs to address the server. A uid
/// that is not a UUID is refused, because a value that cannot round-trip must
/// not become a key.
class Uid {
public:
    static constexpr std::size_t textLength = 36;
    static constexpr std::size_t byteLength = 16;

    Uid() = default;

    /// Parse the dashed (`8-4-4-4-12`) or the bare hexadecimal form.
    [[nodiscard]] static std::expected<Uid, StoreError> parse(std::string_view text) noexcept;

    [[nodiscard]] const std::array<std::byte, byteLength> &bytes() const noexcept { return bytes_; }

    /// The dashed form, ready for a path segment.
    [[nodiscard]] std::string text() const;

    [[nodiscard]] bool empty() const noexcept;

    friend bool operator==(const Uid &, const Uid &) noexcept = default;

private:
    std::array<std::byte, byteLength> bytes_{};
};

struct UidHash {
    [[nodiscard]] std::size_t operator()(const Uid &uid) const noexcept;
};

} // namespace antares::domain::k8s
