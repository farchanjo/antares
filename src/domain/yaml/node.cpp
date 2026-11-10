#include "domain/yaml/node.hpp"

#include <cstdint>
#include <expected>
#include <utility>

namespace antares::domain::yaml {

namespace {

struct Line {
    std::size_t indent = 0;
    std::string_view text;
    std::size_t number = 0;
};

[[nodiscard]] bool blank(std::string_view text) noexcept
{
    for (const char value : text) {
        if (value != ' ' && value != '\t' && value != '\r') {
            return false;
        }
    }
    return true;
}

/// Strip a trailing comment. A `#` only starts one when preceded by space or at
/// the start of the line, so a value may still contain `#`.
[[nodiscard]] std::string_view stripComment(std::string_view text) noexcept
{
    for (std::size_t at = 0; at < text.size(); ++at) {
        if (text[at] != '#') {
            continue;
        }
        const bool atStart = at == 0;
        const bool afterSpace = at > 0 && (text[at - 1] == ' ' || text[at - 1] == '\t');
        if (atStart || afterSpace) {
            return text.substr(0, at);
        }
    }
    return text;
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

/// Remove one pair of matching quotes. Anything else is taken literally.
[[nodiscard]] std::string_view unquote(std::string_view text) noexcept
{
    if (text.size() >= 2) {
        const char first = text.front();
        const char last = text.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            text.remove_prefix(1);
            text.remove_suffix(1);
        }
    }
    return text;
}

} // namespace

class Reader {
public:
    explicit Reader(std::string_view text) { split(text); }

    [[nodiscard]] std::expected<Node, ParseError> document()
    {
        if (lines_.empty()) {
            return Node{};
        }
        std::size_t index = 0;
        auto root = readNode(index, lines_.front().indent, 0);
        if (!root) {
            return root;
        }
        if (index < lines_.size()) {
            return fail("content outside any block", lines_[index].number);
        }
        return root;
    }

private:
    [[nodiscard]] std::unexpected<ParseError> fail(std::string detail, std::size_t line) const
    {
        return std::unexpected(ParseError{std::move(detail), line});
    }

    void split(std::string_view text)
    {
        std::size_t start = 0;
        std::size_t number = 0;
        while (start <= text.size()) {
            const std::size_t newline = text.find('\n', start);
            const std::size_t stop = newline == std::string_view::npos ? text.size() : newline;
            ++number;
            const std::string_view raw = text.substr(start, stop - start);

            // A document marker is allowed and carries no meaning for us.
            const std::string_view body = stripComment(raw);
            if (!blank(body) && trim(body) != "---" && trim(body) != "...") {
                std::size_t indent = 0;
                while (indent < body.size() && body[indent] == ' ') {
                    ++indent;
                }
                lines_.push_back(Line{indent, trim(body.substr(indent)), number});
            }

            if (newline == std::string_view::npos) {
                break;
            }
            start = newline + 1;
        }
    }

    /// True when the line at `index` opens a sequence item.
    [[nodiscard]] bool isSequenceItem(std::size_t index) const noexcept
    {
        return index < lines_.size() && !lines_[index].text.empty() &&
               lines_[index].text.front() == '-' &&
               (lines_[index].text.size() == 1 || lines_[index].text[1] == ' ');
    }

    [[nodiscard]] std::expected<Node, ParseError> readNode(std::size_t &index,
                                                           std::size_t indent,
                                                           std::size_t depth)
    {
        if (depth > 32) {
            return fail("nesting ceiling reached", lines_[index].number);
        }
        if (isSequenceItem(index)) {
            return readSequence(index, indent, depth);
        }
        return readMapping(index, indent, depth);
    }

    [[nodiscard]] std::expected<Node, ParseError> readSequence(std::size_t &index,
                                                               std::size_t indent,
                                                               std::size_t depth)
    {
        Node node;
        node.kind_ = Node::Kind::sequence;

        while (index < lines_.size() && lines_[index].indent == indent && isSequenceItem(index)) {
            const std::size_t number = lines_[index].number;
            const std::string_view rest = trim(lines_[index].text.substr(1));

            if (rest.empty()) {
                // `-` alone: the item is the block that follows, indented deeper.
                ++index;
                if (index >= lines_.size() || lines_[index].indent <= indent) {
                    node.items_.emplace_back();
                    continue;
                }
                auto item = readNode(index, lines_[index].indent, depth + 1);
                if (!item) {
                    return item;
                }
                node.items_.push_back(std::move(*item));
                continue;
            }

            // `- key: value` starts a mapping on the item's own line; the rest
            // of that mapping is indented to the item's own column.
            const std::size_t column = indent + (lines_[index].text.size() - rest.size());
            lines_[index].indent = column;
            lines_[index].text = rest;

            auto item = readNode(index, column, depth + 1);
            if (!item) {
                return item;
            }
            node.items_.push_back(std::move(*item));
            (void)number;
        }
        return node;
    }

    [[nodiscard]] std::expected<Node, ParseError> readMapping(std::size_t &index,
                                                              std::size_t indent,
                                                              std::size_t depth)
    {
        Node node;
        node.kind_ = Node::Kind::mapping;

        while (index < lines_.size() && lines_[index].indent == indent) {
            if (isSequenceItem(index)) {
                break;
            }
            const Line &line = lines_[index];
            const std::size_t colon = line.text.find(':');
            if (colon == std::string_view::npos) {
                return fail("expected 'key: value'", line.number);
            }

            const std::string key{unquote(trim(line.text.substr(0, colon)))};
            const std::string_view rest = trim(line.text.substr(colon + 1));
            const std::size_t number = line.number;
            ++index;

            if (!rest.empty()) {
                Node leaf;
                leaf.kind_ = Node::Kind::scalar;
                leaf.text_ = std::string{unquote(rest)};
                node.members_.emplace_back(key, std::move(leaf));
                continue;
            }

            // An empty value means the block follows, or the key is null. A
            // sequence may sit at its parent's own indentation, which is how
            // every kubeconfig writes `clusters:` followed by `- name: ...`.
            if (index < lines_.size() && lines_[index].indent == indent &&
                isSequenceItem(index)) {
                auto child = readSequence(index, indent, depth + 1);
                if (!child) {
                    return child;
                }
                node.members_.emplace_back(key, std::move(*child));
                continue;
            }
            if (index >= lines_.size() || lines_[index].indent <= indent) {
                node.members_.emplace_back(key, Node{});
                continue;
            }
            auto child = readNode(index, lines_[index].indent, depth + 1);
            if (!child) {
                return child;
            }
            node.members_.emplace_back(key, std::move(*child));
            (void)number;
        }
        return node;
    }

    std::vector<Line> lines_;
};

const Node &Node::at(std::string_view key) const noexcept
{
    static const Node absent;
    if (kind_ != Kind::mapping) {
        return absent;
    }
    for (const auto &[name, value] : members_) {
        if (name == key) {
            return value;
        }
    }
    return absent;
}

std::string_view Node::textAt(std::string_view key) const noexcept
{
    const Node &node = at(key);
    if (node.kind_ != Kind::scalar) {
        return {};
    }
    return node.text_;
}

bool Node::boolAt(std::string_view key, bool fallback) const noexcept
{
    const std::string_view text = textAt(key);
    if (text == "true") {
        return true;
    }
    if (text == "false") {
        return false;
    }
    return fallback;
}

std::expected<Node, ParseError> Node::parse(std::string_view text)
{
    Reader reader{text};
    return reader.document();
}

} // namespace antares::domain::yaml
