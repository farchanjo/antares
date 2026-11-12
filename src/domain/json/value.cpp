#include "domain/json/value.hpp"

#include <charconv>
#include <expected>
#include <system_error>

namespace antares::domain::json {

namespace {

[[nodiscard]] bool isWhitespace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

[[nodiscard]] bool isDigit(char value) noexcept
{
    return value >= '0' && value <= '9';
}

[[nodiscard]] int hexValue(char value) noexcept
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return 10 + (value - 'a');
    }
    if (value >= 'A' && value <= 'F') {
        return 10 + (value - 'A');
    }
    return -1;
}

void appendUtf8(std::string &out, std::uint32_t code)
{
    if (code < 0x80U) {
        out += static_cast<char>(code);
        return;
    }
    if (code < 0x800U) {
        out += static_cast<char>(0xC0U | (code >> 6U));
        out += static_cast<char>(0x80U | (code & 0x3FU));
        return;
    }
    if (code < 0x10000U) {
        out += static_cast<char>(0xE0U | (code >> 12U));
        out += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
        out += static_cast<char>(0x80U | (code & 0x3FU));
        return;
    }
    out += static_cast<char>(0xF0U | (code >> 18U));
    out += static_cast<char>(0x80U | ((code >> 12U) & 0x3FU));
    out += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
    out += static_cast<char>(0x80U | (code & 0x3FU));
}

constexpr std::size_t DEPTH_CEILING = 64;

} // namespace

class Reader {
public:
    explicit Reader(std::string_view text) : text_(text) {}

    [[nodiscard]] std::expected<Value, ParseError> document()
    {
        skipWhitespace();
        auto value = readValue(0);
        if (!value) {
            return value;
        }
        skipWhitespace();
        if (at_ != text_.size()) {
            return fail("trailing content after the document");
        }
        return value;
    }

private:
    [[nodiscard]] std::unexpected<ParseError> fail(std::string detail) const
    {
        return std::unexpected(ParseError{std::move(detail), at_});
    }

    void skipWhitespace()
    {
        while (at_ < text_.size() && isWhitespace(text_[at_])) {
            ++at_;
        }
    }

    [[nodiscard]] std::expected<Value, ParseError> readValue(std::size_t depth)
    {
        if (depth >= DEPTH_CEILING) {
            return fail("nesting ceiling reached");
        }
        skipWhitespace();
        if (at_ >= text_.size()) {
            return fail("unexpected end of document");
        }

        switch (text_[at_]) {
        case '{':
            return readObject(depth);
        case '[':
            return readArray(depth);
        case '"':
            return readStringValue();
        case 't':
            return readLiteral("true", Value{true});
        case 'f':
            return readLiteral("false", Value{false});
        case 'n':
            return readLiteral("null", Value{});
        default:
            break;
        }
        if (text_[at_] == '-' || isDigit(text_[at_])) {
            return readNumber();
        }
        return fail("unexpected character");
    }

    [[nodiscard]] std::expected<Value, ParseError> readLiteral(std::string_view token, Value value)
    {
        if (text_.substr(at_, token.size()) != token) {
            return fail("bad literal");
        }
        at_ += token.size();
        return value;
    }

    [[nodiscard]] std::expected<Value, ParseError> readNumber()
    {
        const std::size_t start = at_;
        if (at_ < text_.size() && text_[at_] == '-') {
            ++at_;
        }
        while (at_ < text_.size() && isDigit(text_[at_])) {
            ++at_;
        }
        if (at_ < text_.size() && text_[at_] == '.') {
            ++at_;
            while (at_ < text_.size() && isDigit(text_[at_])) {
                ++at_;
            }
        }
        if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
            ++at_;
            if (at_ < text_.size() && (text_[at_] == '+' || text_[at_] == '-')) {
                ++at_;
            }
            while (at_ < text_.size() && isDigit(text_[at_])) {
                ++at_;
            }
        }
        if (at_ == start) {
            return fail("empty number");
        }

        Value value;
        value.kind_ = Value::Kind::number;
        value.text_ = std::string{text_.substr(start, at_ - start)};
        return value;
    }

    [[nodiscard]] std::expected<Value, ParseError> readStringValue()
    {
        auto text = readString();
        if (!text) {
            return std::unexpected(text.error());
        }
        Value value;
        value.kind_ = Value::Kind::string;
        value.text_ = std::move(*text);
        return value;
    }

    [[nodiscard]] std::expected<std::string, ParseError> readString()
    {
        ++at_; // opening quote
        std::string out;
        while (at_ < text_.size()) {
            const char value = text_[at_++];
            if (value == '"') {
                return out;
            }
            if (value != '\\') {
                out += value;
                continue;
            }
            if (at_ >= text_.size()) {
                return fail("unterminated escape");
            }
            const char escape = text_[at_++];
            switch (escape) {
            case '"':
            case '\\':
            case '/':
                out += escape;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u':
                if (auto code = readUnicode(); code) {
                    // A surrogate pair is one code point, so the low half is
                    // consumed here and the pair encodes as four bytes. The
                    // scanner does the same, which is what keeps the two paths
                    // byte-equal (FR-001, feature 008).
                    if (auto point = combineSurrogate(*code); point) {
                        appendUtf8(out, *point);
                    } else {
                        return std::unexpected(point.error());
                    }
                } else {
                    return std::unexpected(code.error());
                }
                break;
            default:
                return fail("unknown escape");
            }
        }
        return fail("unterminated string");
    }

    /// A high surrogate followed by `\u` and a low one is one code point.
    /// Any other shape leaves the unit as it stands, which is the lenient
    /// half of the contract: the caller's document still parses.
    [[nodiscard]] std::expected<std::uint32_t, ParseError> combineSurrogate(std::uint32_t code)
    {
        constexpr std::uint32_t HIGH_FIRST = 0xD800U;
        constexpr std::uint32_t HIGH_LAST = 0xDBFFU;
        constexpr std::uint32_t LOW_FIRST = 0xDC00U;
        constexpr std::uint32_t LOW_LAST = 0xDFFFU;
        constexpr std::uint32_t BASE = 0x10000U;

        if (code < HIGH_FIRST || code > HIGH_LAST) {
            return code;
        }
        if (at_ + 1 >= text_.size() || text_[at_] != '\\' || text_[at_ + 1] != 'u') {
            return code;
        }
        at_ += 2;
        auto low = readUnicode();
        if (!low) {
            return std::unexpected(low.error());
        }
        if (*low < LOW_FIRST || *low > LOW_LAST) {
            return code;
        }
        return BASE + ((code - HIGH_FIRST) << 10U) + (*low - LOW_FIRST);
    }

    [[nodiscard]] std::expected<std::uint32_t, ParseError> readUnicode()    {
        if (at_ + 4 > text_.size()) {
            return fail("short unicode escape");
        }
        std::uint32_t code = 0;
        for (int index = 0; index < 4; ++index) {
            const int digit = hexValue(text_[at_ + static_cast<std::size_t>(index)]);
            if (digit < 0) {
                return fail("bad unicode escape");
            }
            code = (code << 4U) | static_cast<std::uint32_t>(digit);
        }
        at_ += 4;
        return code;
    }

    [[nodiscard]] std::expected<Value, ParseError> readArray(std::size_t depth)
    {
        ++at_; // '['
        Value value;
        value.kind_ = Value::Kind::array;

        skipWhitespace();
        if (at_ < text_.size() && text_[at_] == ']') {
            ++at_;
            return value;
        }

        while (true) {
            auto item = readValue(depth + 1);
            if (!item) {
                return item;
            }
            value.items_.push_back(std::move(*item));

            skipWhitespace();
            if (at_ >= text_.size()) {
                return fail("unterminated array");
            }
            if (text_[at_] == ',') {
                ++at_;
                continue;
            }
            if (text_[at_] == ']') {
                ++at_;
                return value;
            }
            return fail("expected ',' or ']'");
        }
    }

    [[nodiscard]] std::expected<Value, ParseError> readObject(std::size_t depth)
    {
        ++at_; // '{'
        Value value;
        value.kind_ = Value::Kind::object;

        skipWhitespace();
        if (at_ < text_.size() && text_[at_] == '}') {
            ++at_;
            return value;
        }

        while (true) {
            skipWhitespace();
            if (at_ >= text_.size() || text_[at_] != '"') {
                return fail("expected a member name");
            }
            auto key = readString();
            if (!key) {
                return std::unexpected(key.error());
            }

            skipWhitespace();
            if (at_ >= text_.size() || text_[at_] != ':') {
                return fail("expected ':'");
            }
            ++at_;

            auto item = readValue(depth + 1);
            if (!item) {
                return item;
            }
            value.members_.emplace_back(std::move(*key), std::move(*item));

            skipWhitespace();
            if (at_ >= text_.size()) {
                return fail("unterminated object");
            }
            if (text_[at_] == ',') {
                ++at_;
                continue;
            }
            if (text_[at_] == '}') {
                ++at_;
                return value;
            }
            return fail("expected ',' or '}'");
        }
    }

    std::string_view text_;
    std::size_t at_ = 0;
};

std::optional<std::int64_t> Value::asInt() const noexcept
{
    if (kind_ != Kind::number || text_.empty()) {
        return std::nullopt;
    }
    std::int64_t out = 0;
    const char *first = text_.data();
    const char *last = first + text_.size();
    const auto result = std::from_chars(first, last, out);
    if (result.ec != std::errc{} || result.ptr != last) {
        return std::nullopt;
    }
    return out;
}

const Value &Value::member(std::string_view key) const noexcept
{
    static const Value absent;
    if (kind_ != Kind::object) {
        return absent;
    }
    for (const auto &[name, value] : members_) {
        if (name == key) {
            return value;
        }
    }
    return absent;
}

std::string_view Value::textAt(std::string_view key) const noexcept
{
    const Value &value = member(key);
    if (value.kind_ != Kind::string) {
        return {};
    }
    return value.text_;
}

std::optional<std::int64_t> Value::intAt(std::string_view key) const noexcept
{
    return member(key).asInt();
}

std::expected<Value, ParseError> Value::parse(std::string_view text)
{
    Reader reader{text};
    return reader.document();
}

std::string Value::dump() const
{
    switch (kind_) {
    case Kind::null:
        return "null";
    case Kind::boolean:
        return boolean_ ? "true" : "false";
    case Kind::number:
        return text_.empty() ? "0" : text_;
    case Kind::string:
        return quote(text_);
    case Kind::array:
    case Kind::object:
        break;
    }

    const bool isArray = kind_ == Kind::array;
    std::string out = isArray ? "[" : "{";
    if (isArray) {
        for (std::size_t at = 0; at < items_.size(); ++at) {
            if (at != 0) {
                out += ',';
            }
            out += items_[at].dump();
        }
    } else {
        for (std::size_t at = 0; at < members_.size(); ++at) {
            if (at != 0) {
                out += ',';
            }
            out += quote(members_[at].first);
            out += ':';
            out += members_[at].second.dump();
        }
    }
    out += isArray ? "]" : "}";
    return out;
}

std::string quote(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 2);
    out += '"';
    for (const char value : text) {
        switch (value) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(value) < 0x20U) {
                constexpr std::string_view DIGITS = "0123456789abcdef";
                const auto byte = static_cast<unsigned char>(value);
                out += "\\u00";
                out += DIGITS[byte >> 4U];
                out += DIGITS[byte & 0x0FU];
                break;
            }
            out += value;
            break;
        }
    }
    out += '"';
    return out;
}

} // namespace antares::domain::json
