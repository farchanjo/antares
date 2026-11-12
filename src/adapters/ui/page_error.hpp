#pragma once

#include <cstdint>
#include <string_view>

namespace antares::adapters::ui {

/// Why a page did not arrive.
///
/// The family, never the payload: the retry row shows one of these names and
/// the log line repeats it, so an error body cannot grow the row (FR-009 of the
/// security requirements).
enum class PageError : std::uint8_t {
    /// The transport or the stream failed.
    transport,
    /// The server answered with an API error.
    refused,
    /// The scope is no longer open.
    unknownScope,
};

[[nodiscard]] std::string_view pageErrorName(PageError error) noexcept;

} // namespace antares::adapters::ui
