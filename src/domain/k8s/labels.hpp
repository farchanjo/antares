#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "domain/k8s/label.hpp"

namespace antares::domain::k8s {

/// The label set of one stored object, kept as a single blob.
///
/// A label set is the biggest variable part of an entry and a single label may
/// legally reach 380 bytes, so the set is only bounded by cutting it: at 16
/// entries or 256 bytes of key and value, whichever comes first, and
/// `truncated()` says the cut happened (FR-003).
///
/// The blob holds `key=value` records separated by `\n`. Label keys and values
/// are DNS-ish by the server's own rules, so neither separator can occur inside
/// a record. Storing one string instead of a vector of strings is what keeps a
/// worst-case entry inside its ceiling: sixteen pairs cost 256 bytes rather than
/// sixteen separately allocated strings.
class Labels {
public:
    static constexpr std::size_t maxEntries = 16;
    static constexpr std::size_t maxBytes = 256;

    Labels() = default;

    /// Keep one pair, or record the cut. Returns false once a ceiling is hit;
    /// the caller keeps calling but every later pair is cut too.
    bool append(std::string_view key, std::string_view value);

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] bool truncated() const noexcept { return truncated_; }

    /// The record at `index`, or nothing past the end.
    [[nodiscard]] std::optional<Label> at(std::size_t index) const noexcept;

    /// The value kept for `key`, or an empty view when it was not kept.
    [[nodiscard]] std::string_view valueOf(std::string_view key) const noexcept;

    /// The raw records, for a caller that renders the whole set.
    [[nodiscard]] std::string_view blob() const noexcept { return blob_; }

private:
    std::string blob_;
    std::uint16_t used_ = 0;
    std::uint8_t count_ = 0;
    bool truncated_ = false;
};

} // namespace antares::domain::k8s
