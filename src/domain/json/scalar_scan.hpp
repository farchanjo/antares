#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "domain/json/scan_error.hpp"

namespace antares::domain::json {

namespace {

[[nodiscard, gnu::always_inline]] inline bool isWhitespace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

[[nodiscard, gnu::always_inline]] inline bool isDigit(char value) noexcept
{
    return value >= '0' && value <= '9';
}

[[nodiscard, gnu::always_inline]] inline int hexValue(char value) noexcept
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

/// The UTF-16 surrogate range, named so the pair test reads as one.
constexpr std::uint32_t HIGH_SURROGATE_FIRST = 0xD800U;
constexpr std::uint32_t HIGH_SURROGATE_LAST = 0xDBFFU;
constexpr std::uint32_t LOW_SURROGATE_FIRST = 0xDC00U;
constexpr std::uint32_t LOW_SURROGATE_LAST = 0xDFFFU;
constexpr std::uint32_t SUPPLEMENTARY_BASE = 0x10000U;
constexpr std::uint32_t SURROGATE_SHIFT = 10U;

[[nodiscard, gnu::always_inline]] inline bool isHighSurrogate(std::uint32_t code) noexcept
{
    return code >= HIGH_SURROGATE_FIRST && code <= HIGH_SURROGATE_LAST;
}

[[nodiscard, gnu::always_inline]] inline bool isLowSurrogate(std::uint32_t code) noexcept
{
    return code >= LOW_SURROGATE_FIRST && code <= LOW_SURROGATE_LAST;
}

[[gnu::always_inline]] inline void appendUtf8(std::string &out, std::uint32_t code)
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

/// The three byte classes that end a plain string run: the closing quote, the
/// escape backslash and the whole control range. The word-at-a-time test is
/// what makes a two-kilobyte annotation cost microseconds, not tens of them.
[[nodiscard, gnu::always_inline]] inline std::size_t runEnd(std::string_view text,
                                                            std::size_t at) noexcept
{
    constexpr std::uint64_t LOW_BITS = 0x0101010101010101ULL;
    constexpr std::uint64_t HIGH_BITS = 0x8080808080808080ULL;
    constexpr std::uint64_t CONTROL = 0x2020202020202020ULL;
    constexpr std::uint64_t QUOTE = 0x2222222222222222ULL;
    constexpr std::uint64_t BACKSLASH = 0x5C5C5C5C5C5C5C5CULL;

    while (at + sizeof(std::uint64_t) <= text.size()) {
        std::uint64_t word = 0;
        __builtin_memcpy(&word, text.data() + at, sizeof(word));
        // `hasless` detects a byte under 0x20; the two XORs detect the quote
        // and the backslash. Any hit drops to the byte loop below.
        const std::uint64_t control = (word - CONTROL) & ~word & HIGH_BITS;
        const std::uint64_t quote = word ^ QUOTE;
        const std::uint64_t backslash = word ^ BACKSLASH;
        const std::uint64_t special = ((quote - LOW_BITS) & ~quote & HIGH_BITS) |
                                      ((backslash - LOW_BITS) & ~backslash & HIGH_BITS);
        if ((control | special) != 0) {
            break;
        }
        at += sizeof(std::uint64_t);
    }
    while (at < text.size() && text[at] != '"' && text[at] != '\\' &&
           static_cast<unsigned char>(text[at]) >= 0x20U) {
        ++at;
    }
    return at;
}

} // namespace

/// A cursor that reads one value at a time and can step over a value whole.
///
/// This is the selective scan of feature 008: the document stays the bytes it
/// arrived as, and a value nobody renders is validated in place instead of
/// being built (FR-002). Only the fields a caller decodes allocate, and only
/// into the buffer the caller hands over.
///
/// Every step reports failure through a `bool` and the `error()` that goes with
/// it, never through a returned `std::expected`: a member of a skipped object
/// costs no `expected` construction, and that difference is most of the 8x the
/// feature exists for. `decodeEntry` adds the typed surface back once per
/// document.
///
/// The cursor is a `std::string_view` and an offset, so a copy is a second
/// cursor over the same bytes — the entry decoder uses that to bound a value
/// before it scans it, and to fall back to `Value::parse` for a shape the
/// scanner refuses (FR-001).
///
/// The two decode buffers are members rather than return values: the views
/// `memberName` and `stringValue` hand out are valid until the next call on the
/// same buffer, and a caller that keeps a decoded text copies it.
class ScalarScan {
public:
    /// What the separator walk found: another entry, the closing delimiter, or
    /// a failure `error_` already describes.
    enum class EntryState : std::uint8_t { more, closed, failed };

    /// The same bound `Value::parse` uses, so a document one parser accepts
    /// cannot be refused by the other for depth alone (FR-003).
    static constexpr std::size_t depthCeiling = 64;

    static constexpr std::size_t hexDigits = 4;

    explicit ScalarScan(std::string_view text) noexcept : text_(text) {}

    ScalarScan(const ScalarScan &) = default;
    ScalarScan &operator=(const ScalarScan &) = default;
    ScalarScan(ScalarScan &&) = default;
    ScalarScan &operator=(ScalarScan &&) = default;
    ~ScalarScan() = default;

    [[nodiscard]] std::string_view text() const noexcept { return text_; }
    [[nodiscard]] std::size_t offset() const noexcept { return at_; }

    /// Move the cursor without validating what it crosses. Used to commit a
    /// probe and to recover the start of a value.
    void advanceTo(std::size_t offset) noexcept;

    /// What the step that returned false refused.
    [[nodiscard]] const ScanError &error() const noexcept { return error_; }

    /// Consume `open` (`{` or `[`) after whitespace.
    [[nodiscard]] bool begin(char open) { return beginRaw(open); }

    /// Consume the separator before the next entry and say whether one follows.
    /// `first` is true just after `begin`; a `,` immediately before `close` is
    /// a trailing comma and refused.
    [[nodiscard]] EntryState nextEntry(char close, bool first) { return nextEntryRaw(close, first); }

    /// Read a member name and its `:`; the view is into the document or the
    /// name buffer.
    [[nodiscard]] bool memberName(std::string_view &out);

    /// Read a string value, unescaped (FR-004); the view is into the document
    /// or the value buffer.
    [[nodiscard]] bool stringValue(std::string_view &out);

    /// Read a number as a signed integer. A number that does not fit yields
    /// nothing, which is `Value::asInt`'s answer too; a fraction or an
    /// exponent is refused, and the caller's fallback covers it.
    [[nodiscard]] bool intValue(std::optional<std::int64_t> &out);

    /// Step over the next value by shape, validating it (FR-003).
    [[nodiscard]] bool skipValue(std::size_t depth) { return skipValueRaw(depth); }

    /// Whitespace, then the document must end.
    [[nodiscard]] bool finish();

private:
    [[nodiscard]] bool fail(ScanReason reason) noexcept;

    void skipWhitespace() noexcept;

    [[nodiscard]] bool beginRaw(char open);
    [[nodiscard]] EntryState nextEntryRaw(char close, bool first);
    [[nodiscard]] bool skipValueRaw(std::size_t depth);
    [[nodiscard]] bool skipObjectRaw(std::size_t depth);
    [[nodiscard]] bool skipArrayRaw(std::size_t depth);
    [[nodiscard]] bool skipNumberRaw();
    [[nodiscard]] bool skipLiteralRaw(std::string_view token);

    /// Read a string; `out` is null when the bytes are validated and dropped,
    /// which is the skip path and allocates nothing.
    [[nodiscard]] bool readStringRaw(std::string *out);

    /// Decode one escape, the backslash already consumed.
    [[nodiscard]] bool readEscapeRaw(std::string *out);

    [[nodiscard]] bool readHex4Raw(std::uint32_t &out);

    /// The `\uXXXX` that must follow a high surrogate (FR-004).
    [[nodiscard]] bool readLowSurrogateRaw(std::uint32_t &out);

    std::string_view text_;
    std::size_t at_ = 0;
    ScanError error_{};
    std::string name_;
    std::string value_;
};

[[gnu::always_inline]] inline void ScalarScan::advanceTo(std::size_t offset) noexcept
{
    at_ = offset <= text_.size() ? offset : text_.size();
}

[[gnu::always_inline]] inline bool ScalarScan::fail(ScanReason reason) noexcept
{
    error_ = ScanError{reason, at_};
    return false;
}

[[gnu::always_inline]] inline void ScalarScan::skipWhitespace() noexcept
{
    while (at_ < text_.size() && isWhitespace(text_[at_])) {
        ++at_;
    }
}

[[gnu::always_inline]] inline bool ScalarScan::beginRaw(char open)
{
    skipWhitespace();
    if (at_ >= text_.size()) {
        return fail(ScanReason::unexpectedEnd);
    }
    if (text_[at_] != open) {
        return fail(ScanReason::unexpectedCharacter);
    }
    ++at_;
    return true;
}

[[gnu::always_inline]] inline ScalarScan::EntryState ScalarScan::nextEntryRaw(char close, bool first)
{
    skipWhitespace();
    if (at_ >= text_.size()) {
        static_cast<void>(fail(ScanReason::unexpectedEnd));
        return EntryState::failed;
    }
    if (text_[at_] == close) {
        ++at_;
        return EntryState::closed;
    }
    if (first) {
        return EntryState::more;
    }
    if (text_[at_] != ',') {
        static_cast<void>(fail(ScanReason::expectedSeparator));
        return EntryState::failed;
    }
    ++at_;
    skipWhitespace();
    if (at_ < text_.size() && text_[at_] == close) {
        static_cast<void>(fail(ScanReason::trailingComma));
        return EntryState::failed;
    }
    return EntryState::more;
}

[[gnu::always_inline]] inline bool ScalarScan::readStringRaw(std::string *out)
{
    skipWhitespace();
    if (at_ >= text_.size()) {
        return fail(ScanReason::unexpectedEnd);
    }
    if (text_[at_] != '"') {
        return fail(ScanReason::expectedMemberName);
    }
    ++at_;
    if (out != nullptr) {
        out->clear();
    }

    // A run is located eight bytes at a time and appended once, so the common
    // byte costs a fraction of a compare and never a call into `std::string`.
    while (true) {
        const std::size_t end = runEnd(text_, at_);
        if (out != nullptr && end > at_) {
            out->append(text_.substr(at_, end - at_));
        }
        at_ = end;
        if (at_ >= text_.size()) {
            return fail(ScanReason::unterminatedString);
        }
        if (text_[at_] == '"') {
            ++at_;
            return true;
        }
        ++at_; // the backslash
        if (!readEscapeRaw(out)) {
            return false;
        }
    }
}

[[gnu::always_inline]] inline bool ScalarScan::readEscapeRaw(std::string *out)
{
    if (at_ >= text_.size()) {
        return fail(ScanReason::unexpectedEnd);
    }
    const char escape = text_[at_++];
    char decoded = '\0';
    switch (escape) {
    case '"':
    case '\\':
    case '/':
        decoded = escape;
        break;
    case 'b':
        decoded = '\b';
        break;
    case 'f':
        decoded = '\f';
        break;
    case 'n':
        decoded = '\n';
        break;
    case 'r':
        decoded = '\r';
        break;
    case 't':
        decoded = '\t';
        break;
    case 'u':
        break;
    default:
        return fail(ScanReason::badEscape);
    }

    if (escape != 'u') {
        if (out != nullptr) {
            *out += decoded;
        }
        return true;
    }

    std::uint32_t point = 0;
    if (!readHex4Raw(point)) {
        return false;
    }
    if (isHighSurrogate(point)) {
        std::uint32_t low = 0;
        if (!readLowSurrogateRaw(low)) {
            return false;
        }
        point = SUPPLEMENTARY_BASE + ((point - HIGH_SURROGATE_FIRST) << SURROGATE_SHIFT) +
                (low - LOW_SURROGATE_FIRST);
    } else if (isLowSurrogate(point)) {
        return fail(ScanReason::loneSurrogate);
    }
    if (out != nullptr) {
        appendUtf8(*out, point);
    }
    return true;
}

[[gnu::always_inline]] inline bool ScalarScan::readHex4Raw(std::uint32_t &out)
{
    if (at_ + hexDigits > text_.size()) {
        return fail(ScanReason::badUnicodeEscape);
    }
    std::uint32_t code = 0;
    for (std::size_t index = 0; index < hexDigits; ++index) {
        const int digit = hexValue(text_[at_ + index]);
        if (digit < 0) {
            return fail(ScanReason::badUnicodeEscape);
        }
        code = (code << 4U) | static_cast<std::uint32_t>(digit);
    }
    at_ += hexDigits;
    out = code;
    return true;
}

[[gnu::always_inline]] inline bool ScalarScan::readLowSurrogateRaw(std::uint32_t &out)
{
    if (at_ + 1 >= text_.size() || text_[at_] != '\\' || text_[at_ + 1] != 'u') {
        return fail(ScanReason::loneSurrogate);
    }
    at_ += 2;
    if (!readHex4Raw(out)) {
        return false;
    }
    if (!isLowSurrogate(out)) {
        return fail(ScanReason::loneSurrogate);
    }
    return true;
}

[[gnu::always_inline]] inline bool ScalarScan::skipValueRaw(std::size_t depth)
{
    if (depth >= depthCeiling) {
        return fail(ScanReason::depthCeiling);
    }
    skipWhitespace();
    if (at_ >= text_.size()) {
        return fail(ScanReason::unexpectedEnd);
    }

    switch (text_[at_]) {
    case '{':
        return skipObjectRaw(depth);
    case '[':
        return skipArrayRaw(depth);
    case '"':
        return readStringRaw(nullptr);
    case 't':
        return skipLiteralRaw("true");
    case 'f':
        return skipLiteralRaw("false");
    case 'n':
        return skipLiteralRaw("null");
    default:
        break;
    }
    if (text_[at_] == '-' || isDigit(text_[at_])) {
        return skipNumberRaw();
    }
    return fail(ScanReason::unexpectedCharacter);
}

[[gnu::always_inline]] inline bool ScalarScan::skipObjectRaw(std::size_t depth)
{
    ++at_; // '{'
    bool first = true;
    while (true) {
        const EntryState state = nextEntryRaw('}', first);
        if (state == EntryState::failed) {
            return false;
        }
        if (state == EntryState::closed) {
            return true;
        }
        first = false;
        if (!readStringRaw(&name_)) {
            return false;
        }
        skipWhitespace();
        if (at_ >= text_.size()) {
            return fail(ScanReason::unexpectedEnd);
        }
        if (text_[at_] != ':') {
            return fail(ScanReason::expectedColon);
        }
        ++at_;
        if (!skipValueRaw(depth + 1)) {
            return false;
        }
    }
}

[[gnu::always_inline]] inline bool ScalarScan::skipArrayRaw(std::size_t depth)
{
    ++at_; // '['
    bool first = true;
    while (true) {
        const EntryState state = nextEntryRaw(']', first);
        if (state == EntryState::failed) {
            return false;
        }
        if (state == EntryState::closed) {
            return true;
        }
        first = false;
        if (!skipValueRaw(depth + 1)) {
            return false;
        }
    }
}

[[gnu::always_inline]] inline bool ScalarScan::skipLiteralRaw(std::string_view token)
{
    for (const char wanted : token) {
        if (at_ >= text_.size()) {
            return fail(ScanReason::unexpectedEnd);
        }
        if (text_[at_] != wanted) {
            return fail(ScanReason::unexpectedCharacter);
        }
        ++at_;
    }
    return true;
}

[[gnu::always_inline]] inline bool ScalarScan::skipNumberRaw()
{
    const std::size_t start = at_;
    if (at_ < text_.size() && text_[at_] == '-') {
        ++at_;
    }
    // A leading zero stands alone; the parser accepts `01` as its own text, so
    // a stricter scan only means the fallback covers it.
    if (at_ < text_.size() && text_[at_] == '0') {
        ++at_;
    } else {
        if (at_ >= text_.size() || !isDigit(text_[at_])) {
            return fail(ScanReason::unexpectedCharacter);
        }
        while (at_ < text_.size() && isDigit(text_[at_])) {
            ++at_;
        }
    }
    if (at_ < text_.size() && text_[at_] == '.') {
        ++at_;
        if (at_ >= text_.size() || !isDigit(text_[at_])) {
            return fail(ScanReason::unexpectedCharacter);
        }
        while (at_ < text_.size() && isDigit(text_[at_])) {
            ++at_;
        }
    }
    if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
        ++at_;
        if (at_ < text_.size() && (text_[at_] == '+' || text_[at_] == '-')) {
            ++at_;
        }
        if (at_ >= text_.size() || !isDigit(text_[at_])) {
            return fail(ScanReason::unexpectedCharacter);
        }
        while (at_ < text_.size() && isDigit(text_[at_])) {
            ++at_;
        }
    }
    if (at_ == start) {
        return fail(ScanReason::unexpectedCharacter);
    }
    return true;
}

[[gnu::always_inline]] inline bool ScalarScan::memberName(std::string_view &out)
{
    if (!readStringRaw(&name_)) {
        return false;
    }
    skipWhitespace();
    if (at_ >= text_.size()) {
        return fail(ScanReason::unexpectedEnd);
    }
    if (text_[at_] != ':') {
        return fail(ScanReason::expectedColon);
    }
    ++at_;
    out = std::string_view{name_};
    return true;
}

[[gnu::always_inline]] inline bool ScalarScan::stringValue(std::string_view &out)
{
    if (!readStringRaw(&value_)) {
        return false;
    }
    out = std::string_view{value_};
    return true;
}

[[gnu::always_inline]] inline bool ScalarScan::intValue(std::optional<std::int64_t> &out)
{
    skipWhitespace();
    const std::size_t start = at_;
    if (!skipNumberRaw()) {
        return false;
    }

    std::int64_t parsed = 0;
    const char *first = text_.data() + start;
    const char *last = text_.data() + at_;
    const auto result = std::from_chars(first, last, parsed);
    if (result.ec == std::errc::result_out_of_range) {
        out.reset();
        return true;
    }
    if (result.ec != std::errc{} || result.ptr != last) {
        return fail(ScanReason::unexpectedCharacter);
    }
    out = parsed;
    return true;
}

[[gnu::always_inline]] inline bool ScalarScan::finish()
{
    skipWhitespace();
    if (at_ != text_.size()) {
        return fail(ScanReason::trailingContent);
    }
    return true;
}

} // namespace antares::domain::json
