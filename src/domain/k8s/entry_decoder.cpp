#include "domain/k8s/entry_decoder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "domain/json/value.hpp"
#include "domain/k8s/object_name.hpp"

namespace antares::domain::k8s {

namespace {

/// The two text fields that are not names but are still bounded.
constexpr std::size_t TEXT_CEILING = 64;

/// Cut `text` to at most `ceiling` bytes without splitting a UTF-8 sequence.
[[nodiscard]] std::string_view cut(std::string_view text, std::size_t ceiling) noexcept
{
    if (text.size() <= ceiling) {
        return text;
    }
    std::size_t end = ceiling;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) {
        --end;
    }
    return text.substr(0, end);
}

/// Keep a label set in key order and inside both ceilings. Sorting first is
/// what makes the cut deterministic for a server that reorders the object.
void readLabels(const json::Value &meta, Labels &labels)
{
    const json::Value &source = meta.member("labels");
    if (!source.isObject()) {
        return;
    }

    std::vector<const json::Value::Member *> ordered;
    ordered.reserve(source.members().size());
    for (const auto &member : source.members()) {
        ordered.push_back(&member);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto *left, const auto *right) {
        return left->first < right->first;
    });

    for (const auto *member : ordered) {
        labels.append(member->first, member->second.asText());
    }
}

[[nodiscard]] std::optional<std::uint32_t> readCount(const json::Value &status,
                                                     std::string_view key) noexcept
{
    const auto value = status.intAt(key);
    if (!value || *value < 0) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

/// `status` is read for two counts and a reason and nothing else, so a large
/// `conditions` or `containerStatuses` is dropped with the document.
void readStatus(const json::Value &object, StatusSummary &summary)
{
    const json::Value &status = object.member("status");
    if (!status.isObject()) {
        return;
    }
    summary.reason = std::string{cut(status.textAt("reason"), StatusSummary::reasonCeiling)};
    summary.ready = readCount(status, "readyReplicas");
    summary.total = readCount(status, "replicas");
}

} // namespace

std::expected<Entry, StoreError> decodeEntry(const json::Value &object)
{
    if (!object.isObject()) {
        return std::unexpected(StoreError::invalidUid);
    }

    const json::Value &meta = object.member("metadata");

    auto uid = Uid::parse(meta.textAt("uid"));
    if (!uid) {
        return std::unexpected(uid.error());
    }

    auto name = ObjectName::of(meta.textAt("name"));
    if (!name) {
        return std::unexpected(name.error());
    }

    const std::string_view kind = object.textAt("kind");
    if (kind.empty() || kind.size() > TEXT_CEILING) {
        return std::unexpected(StoreError::invalidKind);
    }

    const std::string_view version = meta.textAt("resourceVersion");
    if (version.size() > TEXT_CEILING) {
        return std::unexpected(StoreError::invalidVersion);
    }

    Entry entry;
    entry.identity.uid = *uid;
    entry.identity.name = name->text();
    entry.resourceVersion = std::string{version};
    entry.generation = static_cast<std::uint64_t>(std::max<std::int64_t>(
        meta.intAt("generation").value_or(0), 0));
    entry.creationTimestamp = std::string{cut(meta.textAt("creationTimestamp"), TEXT_CEILING)};

    readLabels(meta, entry.labels);
    readStatus(object, entry.status);
    return entry;
}

} // namespace antares::domain::k8s
