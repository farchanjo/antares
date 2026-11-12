#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace antares::domain::json {

class Reader;

/// Why a document was refused. A parser that guesses is worse than one that
/// refuses: a mangled field is a silent wrong answer later.
struct ParseError {
    std::string detail;
    std::size_t offset = 0;
};

/// One JSON value (RFC 8259). A number keeps its text, so an integer that does
/// not fit an `int` still survives a round trip.
class Value {
public:
    enum class Kind : std::uint8_t { null, boolean, number, string, array, object };

    Value() = default;

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool isNull() const noexcept { return kind_ == Kind::null; }
    [[nodiscard]] bool isObject() const noexcept { return kind_ == Kind::object; }
    [[nodiscard]] bool isArray() const noexcept { return kind_ == Kind::array; }
    [[nodiscard]] bool isString() const noexcept { return kind_ == Kind::string; }

    [[nodiscard]] bool asBool() const noexcept { return boolean_; }
    [[nodiscard]] std::string_view asText() const noexcept { return text_; }

    /// The integer value of a number, or `std::nullopt` when it is not one or
    /// does not fit.
    [[nodiscard]] std::optional<std::int64_t> asInt() const noexcept;

    [[nodiscard]] const std::vector<Value> &items() const noexcept { return items_; }

    using Member = std::pair<std::string, Value>;

    /// The members of an object, in document order. Empty for anything else.
    /// A caller that needs key order sorts them itself.
    [[nodiscard]] const std::vector<Member> &members() const noexcept { return members_; }

    /// A member of an object, or null when the key is absent or this is not an
    /// object. Returning null for absence keeps the caller free of a second
    /// check.
    [[nodiscard]] const Value &member(std::string_view key) const noexcept;

    /// A member's text, or the empty string when it is absent or not a string.
    [[nodiscard]] std::string_view textAt(std::string_view key) const noexcept;

    /// A member as an integer, or `std::nullopt`.
    [[nodiscard]] std::optional<std::int64_t> intAt(std::string_view key) const noexcept;

    /// Render the value back to text. Used to build a request body, so the
    /// round trip has to be faithful.
    [[nodiscard]] std::string dump() const;

    /// Parse a complete document. Content after the value other than whitespace
    /// is an error, so a concatenated body cannot pass as one document.
    [[nodiscard]] static std::expected<Value, ParseError> parse(std::string_view text);

private:
    friend class Reader;

    explicit Value(bool boolean) noexcept : kind_(Kind::boolean), boolean_(boolean) {}

    Kind kind_ = Kind::null;
    bool boolean_ = false;
    std::string text_;
    std::vector<Value> items_;
    std::vector<std::pair<std::string, Value>> members_;
};

/// Render a JSON string literal, quotes included, escaping what must be escaped.
[[nodiscard]] std::string quote(std::string_view text);

} // namespace antares::domain::json
