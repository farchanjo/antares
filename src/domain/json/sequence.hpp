#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace antares::domain::json {

/// Splits an `application/json-seq` stream (RFC 7464).
///
/// The record separator (0x1E) delimits, and the line feed that the encoder
/// appends after each text is what proves the text was not truncated. A
/// top-level number, `true`, `false`, or `null` is not self-delimiting, so
/// RFC 7464 §2.4 requires dropping one that arrives without its whitespace.
class SequenceReader {
public:
    /// Feed bytes and take whatever complete texts that made. A malformed text
    /// is skipped, not fatal (§2.3), and lands in `rejected`.
    [[nodiscard]] std::vector<std::string> feed(std::string_view chunk);

    /// Texts dropped for being malformed or possibly truncated.
    [[nodiscard]] std::size_t rejected() const noexcept { return rejected_; }

    /// Bytes held back waiting for a delimiter.
    [[nodiscard]] std::size_t pending() const noexcept { return buffer_.size(); }

private:
    std::string buffer_;
    std::size_t rejected_ = 0;
};

/// True when `text` is a complete JSON text on its own. Numbers and the three
/// literals are only complete with trailing whitespace, which the caller is
/// responsible for having checked.
[[nodiscard]] bool isSelfDelimiting(std::string_view text) noexcept;

} // namespace antares::domain::json
