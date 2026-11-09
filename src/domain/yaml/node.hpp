#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace antares::domain::yaml {

/// Why a document was refused. The reader accepts a strict subset, and says so
/// instead of guessing: a kubeconfig misread is a wrong cluster later.
struct ParseError {
    std::string detail;
    std::size_t line = 0;
};

/// One node of a YAML document, reduced to what a kubeconfig uses: a mapping, a
/// sequence, or a scalar kept as text. Anchors, tags, flow style and multi-line
/// scalars are refused rather than half-supported.
class Node {
public:
    enum class Kind : std::uint8_t { null, mapping, sequence, scalar };

    Node() = default;

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool isMapping() const noexcept { return kind_ == Kind::mapping; }
    [[nodiscard]] bool isSequence() const noexcept { return kind_ == Kind::sequence; }

    /// A mapping member, or an empty node when absent.
    [[nodiscard]] const Node &at(std::string_view key) const noexcept;

    /// A member's text, or empty when absent or not a scalar.
    [[nodiscard]] std::string_view textAt(std::string_view key) const noexcept;

    /// A member's boolean, `true`/`false` only; anything else is the fallback.
    [[nodiscard]] bool boolAt(std::string_view key, bool fallback) const noexcept;

    /// A member's scalar, or empty when absent.
    [[nodiscard]] std::string_view text() const noexcept { return text_; }

    [[nodiscard]] const std::vector<Node> &items() const noexcept { return items_; }

    /// Parse a document. Only a single mapping at the root is accepted, which
    /// is what every kubeconfig is.
    [[nodiscard]] static std::expected<Node, ParseError> parse(std::string_view text);

private:
    friend class Reader;

    Kind kind_ = Kind::null;
    std::string text_;
    std::vector<Node> items_;
    std::vector<std::pair<std::string, Node>> members_;
};

} // namespace antares::domain::yaml
