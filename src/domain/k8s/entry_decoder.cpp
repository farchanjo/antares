#include "domain/k8s/entry_decoder.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "domain/json/scalar_scan.hpp"
#include "domain/json/value.hpp"
#include "domain/k8s/labels.hpp"
#include "domain/k8s/object_name.hpp"
#include "domain/k8s/status_summary.hpp"
#include "domain/k8s/uid.hpp"

namespace antares::domain::k8s {

namespace {

using EntryState = json::ScalarScan::EntryState;

/// Room for the keys over the value ceiling, so a full set never re-grows the
/// scratch. A key is short by the server's own rule; this is a hint, not a
/// bound.
constexpr std::size_t KEY_HEADROOM = 64;

/// The two text fields that are not names but are still bounded.
constexpr std::size_t TEXT_CEILING = 64;

/// The nesting a top-level member value and a `metadata` member value sit at.
/// The bound itself is the scanner's; these only keep the call sites honest.
constexpr std::size_t TOP_DEPTH = 1;
constexpr std::size_t META_DEPTH = 2;

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

/// Where one decoded label sits in the scratch blob. Offsets, not pointers, so
/// growing the blob cannot dangle the sort (FR-002: no string per label).
struct LabelRef {
    std::uint32_t keyAt = 0;
    std::uint16_t keySize = 0;
    std::uint32_t valueAt = 0;
    std::uint16_t valueSize = 0;
};

/// Keep a label set in key order and inside both ceilings. Sorting first is
/// what makes the cut deterministic for a server that reorders the object, and
/// the parser path does the same, so both agree byte for byte (FR-001).
///
/// The decoded pairs go into one blob and only offsets are kept, so a label
/// costs two appends rather than two `std::string`s — the label member is the
/// single most expensive part of an entry and this is what keeps it cheap.
/// Keep the `kind` of the first owner reference and skip the rest: the row shows
/// one controller, and a second reference is the exception.
[[nodiscard]] std::expected<void, json::ScanError> scanOwnerKind(json::ScalarScan &scan,
                                                                std::string &kind)
{
    if (!scan.begin('[')) {
        return std::unexpected(scan.error());
    }
    bool first = true;
    bool kept = false;
    while (true) {
        const auto state = scan.nextEntry(']', first);
        if (state != json::ScalarScan::EntryState::more) {
            return {};
        }
        first = false;
        if (!scan.begin('{')) {
            return std::unexpected(scan.error());
        }
        bool firstMember = true;
        while (true) {
            const auto member = scan.nextEntry('}', firstMember);
            if (member != json::ScalarScan::EntryState::more) {
                break;
            }
            firstMember = false;
            std::string_view key;
            if (!scan.memberName(key)) {
                return std::unexpected(scan.error());
            }
            if (key == "kind" && !kept) {
                std::string_view text;
                if (!scan.stringValue(text)) {
                    return std::unexpected(scan.error());
                }
                kind = std::string{cut(text, TEXT_CEILING)};
                kept = true;
                continue;
            }
            if (!scan.skipValue(META_DEPTH)) {
                return std::unexpected(scan.error());
            }
        }
    }
}

[[nodiscard]] std::expected<void, json::ScanError> scanLabels(json::ScalarScan &scan, Labels &labels)
{
    if (!scan.begin('{')) {
        return std::unexpected(scan.error());
    }

    std::string scratch;
    scratch.reserve(Labels::maxBytes + KEY_HEADROOM);
    labels.reserve(Labels::maxBytes);
    std::array<LabelRef, Labels::maxEntries> kept{};
    std::size_t count = 0;
    std::vector<LabelRef> overflow;

    bool first = true;
    while (true) {
        const EntryState state = scan.nextEntry('}', first);
        if (state == EntryState::failed) {
            return std::unexpected(scan.error());
        }
        if (state == EntryState::closed) {
            break;
        }
        first = false;

        std::string_view key;
        if (!scan.memberName(key)) {
            return std::unexpected(scan.error());
        }
        std::string_view value;
        if (!scan.stringValue(value)) {
            return std::unexpected(scan.error());
        }

        LabelRef ref;
        ref.keyAt = static_cast<std::uint32_t>(scratch.size());
        ref.keySize = static_cast<std::uint16_t>(key.size());
        scratch.append(key);
        ref.valueAt = static_cast<std::uint32_t>(scratch.size());
        ref.valueSize = static_cast<std::uint16_t>(value.size());
        scratch.append(value);
        if (count < kept.size()) {
            kept[count++] = ref;
        } else {
            overflow.push_back(ref);
        }
    }

    const auto keyOf = [&scratch](const LabelRef &ref) {
        return std::string_view{scratch}.substr(ref.keyAt, ref.keySize);
    };
    const auto valueOf = [&scratch](const LabelRef &ref) {
        return std::string_view{scratch}.substr(ref.valueAt, ref.valueSize);
    };
    const auto byKey = [&keyOf](const LabelRef &left, const LabelRef &right) {
        return keyOf(left) < keyOf(right);
    };

    if (overflow.empty()) {
        std::sort(kept.begin(), kept.begin() + static_cast<std::ptrdiff_t>(count), byKey);
        for (std::size_t index = 0; index < count; ++index) {
            labels.append(keyOf(kept[index]), valueOf(kept[index]));
        }
        return {};
    }

    // Past the entry ceiling the smallest keys are still the ones kept, so the
    // whole set is ordered before the cut (FR-002).
    overflow.insert(overflow.end(), kept.begin(), kept.begin() + static_cast<std::ptrdiff_t>(count));
    std::sort(overflow.begin(), overflow.end(), byKey);
    for (const LabelRef &ref : overflow) {
        labels.append(keyOf(ref), valueOf(ref));
    }
    return {};
}

/// A count is kept only when it is not negative; a kind that carries neither
/// number leaves both unset (FR-002).
[[nodiscard]] std::expected<void, json::ScanError>
scanCount(json::ScalarScan &scan, std::optional<std::uint32_t> &out)
{
    std::optional<std::int64_t> value;
    if (!scan.intValue(value)) {
        return std::unexpected(scan.error());
    }
    if (value && *value >= 0) {
        out = static_cast<std::uint32_t>(*value);
    }
    return {};
}

/// `status` is read for two counts and a reason and nothing else, so a large
/// `conditions` or `containerStatuses` is stepped over (FR-002).
[[nodiscard]] std::expected<void, json::ScanError> scanStatus(json::ScalarScan &scan,
                                                             StatusSummary &summary)
{
    if (!scan.begin('{')) {
        return std::unexpected(scan.error());
    }

    bool first = true;
    while (true) {
        const EntryState state = scan.nextEntry('}', first);
        if (state == EntryState::failed) {
            return std::unexpected(scan.error());
        }
        if (state == EntryState::closed) {
            return {};
        }
        first = false;

        std::string_view key;
        if (!scan.memberName(key)) {
            return std::unexpected(scan.error());
        }
        if (key == "reason") {
            std::string_view text;
            if (!scan.stringValue(text)) {
                return std::unexpected(scan.error());
            }
            summary.reason = std::string{cut(text, StatusSummary::reasonCeiling)};
            continue;
        }
        if (key == "readyReplicas") {
            auto counted = scanCount(scan, summary.ready);
            if (!counted) {
                return counted;
            }
            continue;
        }
        if (key == "replicas") {
            auto counted = scanCount(scan, summary.total);
            if (!counted) {
                return counted;
            }
            continue;
        }
        if (!scan.skipValue(META_DEPTH)) {
            return std::unexpected(scan.error());
        }
    }
}

/// One `metadata` member, read or stepped over. Split out of the loop so the
/// branch chain does not nest under it.
[[nodiscard]] std::expected<void, json::ScanError>
scanMetadataMember(json::ScalarScan &scan, std::string_view key, Entry &entry, bool &haveUid,
                   bool &haveName)
{
    if (key == "uid") {
        std::string_view text;
        if (!scan.stringValue(text)) {
            return std::unexpected(scan.error());
        }
        auto uid = Uid::parse(text);
        if (!uid) {
            return std::unexpected(json::ScanError{json::ScanReason::refusedUid, scan.offset()});
        }
        entry.identity.uid = *uid;
        haveUid = true;
        return {};
    }
    if (key == "name") {
        std::string_view text;
        if (!scan.stringValue(text)) {
            return std::unexpected(scan.error());
        }
        auto name = ObjectName::of(text);
        if (!name) {
            return std::unexpected(json::ScanError{json::ScanReason::refusedName, scan.offset()});
        }
        entry.identity.name = std::move(*name).take();
        haveName = true;
        return {};
    }
    if (key == "resourceVersion") {
        std::string_view text;
        if (!scan.stringValue(text)) {
            return std::unexpected(scan.error());
        }
        entry.resourceVersion = std::string{text};
        return {};
    }
    if (key == "generation") {
        std::optional<std::int64_t> count;
        if (!scan.intValue(count)) {
            return std::unexpected(scan.error());
        }
        entry.generation = static_cast<std::uint64_t>(std::max<std::int64_t>(count.value_or(0), 0));
        return {};
    }
    if (key == "namespace") {
        std::string_view text;
        if (!scan.stringValue(text)) {
            return std::unexpected(scan.error());
        }
        entry.namespaceName = std::string{cut(text, TEXT_CEILING)};
        return {};
    }
    if (key == "ownerReferences") {
        return scanOwnerKind(scan, entry.ownerKind);
    }
    if (key == "nodeName") {
        std::string_view text;
        if (!scan.stringValue(text)) {
            return std::unexpected(scan.error());
        }
        entry.nodeName = std::string{cut(text, TEXT_CEILING)};
        return {};
    }
    if (key == "qosClass") {
        std::string_view text;
        if (!scan.stringValue(text)) {
            return std::unexpected(scan.error());
        }
        entry.qosClass = std::string{cut(text, TEXT_CEILING)};
        return {};
    }
    if (key == "creationTimestamp") {
        std::string_view text;
        if (!scan.stringValue(text)) {
            return std::unexpected(scan.error());
        }
        entry.creationTimestamp = std::string{cut(text, TEXT_CEILING)};
        return {};
    }
    if (key == "labels") {
        return scanLabels(scan, entry.labels);
    }
    if (!scan.skipValue(META_DEPTH)) {
        return std::unexpected(scan.error());
    }
    return {};
}

/// `metadata` is the one member the entry is built from, so it is walked rather
/// than skipped. A repeated key keeps the last value; no fixture repeats one,
/// and a repeated key is outside the server's contract anyway.
[[nodiscard]] std::expected<void, json::ScanError>
scanMetadata(json::ScalarScan &scan, Entry &entry, bool &haveUid, bool &haveName)
{
    if (!scan.begin('{')) {
        return std::unexpected(scan.error());
    }

    bool first = true;
    while (true) {
        const EntryState state = scan.nextEntry('}', first);
        if (state == EntryState::failed) {
            return std::unexpected(scan.error());
        }
        if (state == EntryState::closed) {
            return {};
        }
        first = false;

        std::string_view key;
        if (!scan.memberName(key)) {
            return std::unexpected(scan.error());
        }
        auto member = scanMetadataMember(scan, key, entry, haveUid, haveName);
        if (!member) {
            return member;
        }
    }
}

/// The parser path of the same two readers: the projection over a built
/// document, kept as the fallback of FR-001.
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

/// A refusal the projection itself made, as the store names it. A shape
/// failure has no entry here, so the caller falls back to the parser (FR-001).
[[nodiscard]] std::optional<StoreError> storeRefusal(json::ScanReason reason) noexcept
{
    switch (reason) {
    case json::ScanReason::refusedUid:
        return StoreError::invalidUid;
    case json::ScanReason::refusedName:
        return StoreError::invalidName;
    case json::ScanReason::refusedKind:
        return StoreError::invalidKind;
    case json::ScanReason::refusedVersion:
        return StoreError::invalidVersion;
    default:
        return std::nullopt;
    }
}

} // namespace

std::expected<Entry, json::ScanError> scanEntry(json::ScalarScan &scan)
{
    if (!scan.begin('{')) {
        return std::unexpected(scan.error());
    }

    Entry entry;
    std::string kind;
    bool haveUid = false;
    bool haveName = false;
    bool first = true;
    while (true) {
        const EntryState state = scan.nextEntry('}', first);
        if (state == EntryState::failed) {
            return std::unexpected(scan.error());
        }
        if (state == EntryState::closed) {
            break;
        }
        first = false;

        std::string_view key;
        if (!scan.memberName(key)) {
            return std::unexpected(scan.error());
        }
        if (key == "metadata") {
            auto meta = scanMetadata(scan, entry, haveUid, haveName);
            if (!meta) {
                return std::unexpected(meta.error());
            }
            continue;
        }
        if (key == "kind") {
            std::string_view text;
            if (!scan.stringValue(text)) {
                return std::unexpected(scan.error());
            }
            kind = std::string{text};
            continue;
        }
        if (key == "status") {
            auto status = scanStatus(scan, entry.status);
            if (!status) {
                return std::unexpected(status.error());
            }
            continue;
        }
        if (!scan.skipValue(TOP_DEPTH)) {
            return std::unexpected(scan.error());
        }
    }

    // The order is the parser path's, so both refuse the same document for the
    // same reason (FR-001).
    if (!haveUid) {
        return std::unexpected(json::ScanError{json::ScanReason::refusedUid, scan.offset()});
    }
    if (!haveName) {
        return std::unexpected(json::ScanError{json::ScanReason::refusedName, scan.offset()});
    }
    // An item of a list carries no `kind`; the scope around it already names the
    // resource, so only an over-long one is refused here.
    if (kind.size() > TEXT_CEILING) {
        return std::unexpected(json::ScanError{json::ScanReason::refusedKind, scan.offset()});
    }
    if (entry.resourceVersion.size() > TEXT_CEILING) {
        return std::unexpected(json::ScanError{json::ScanReason::refusedVersion, scan.offset()});
    }
    return entry;
}

std::expected<Entry, json::ScanError> scanEntry(std::string_view document)
{
    json::ScalarScan scan{document};
    auto entry = scanEntry(scan);
    if (!entry) {
        return entry;
    }
    if (!scan.finish()) {
        return std::unexpected(scan.error());
    }
    return entry;
}

std::expected<std::string, json::ScanError> scanName(std::string_view document,
                                                     std::size_t *consumed)
{
    json::ScalarScan scan{document};
    if (!scan.begin('{')) {
        return std::unexpected(scan.error());
    }

    bool first = true;
    while (true) {
        const EntryState state = scan.nextEntry('}', first);
        if (state == EntryState::failed) {
            return std::unexpected(scan.error());
        }
        if (state == EntryState::closed) {
            break;
        }
        first = false;

        std::string_view key;
        if (!scan.memberName(key)) {
            return std::unexpected(scan.error());
        }
        if (key != "metadata") {
            if (!scan.skipValue(TOP_DEPTH)) {
                return std::unexpected(scan.error());
            }
            continue;
        }

        if (!scan.begin('{')) {
            return std::unexpected(scan.error());
        }
        bool metaFirst = true;
        while (true) {
            const EntryState metaState = scan.nextEntry('}', metaFirst);
            if (metaState == EntryState::failed) {
                return std::unexpected(scan.error());
            }
            if (metaState == EntryState::closed) {
                break;
            }
            metaFirst = false;

            std::string_view metaKey;
            if (!scan.memberName(metaKey)) {
                return std::unexpected(scan.error());
            }
            if (metaKey != "name") {
                if (!scan.skipValue(META_DEPTH)) {
                    return std::unexpected(scan.error());
                }
                continue;
            }
            std::string_view name;
            if (!scan.stringValue(name)) {
                return std::unexpected(scan.error());
            }
            if (consumed != nullptr) {
                *consumed = scan.offset();
            }
            return std::string{name};
        }
        break;
    }

    if (consumed != nullptr) {
        *consumed = scan.offset();
    }
    return std::unexpected(json::ScanError{json::ScanReason::missingName, scan.offset()});
}

std::expected<Entry, StoreError> decodeEntry(std::string_view document, bool *scannerRefused)
{
    auto scanned = scanEntry(document);
    if (scanned) {
        return std::move(*scanned);
    }
    if (const std::optional<StoreError> refusal = storeRefusal(scanned.error().reason)) {
        return std::unexpected(*refusal);
    }
    if (scannerRefused != nullptr) {
        *scannerRefused = true;
    }
    // FR-001 — a shape the scanner refuses goes through the parser. A malformed
    // document fails here too; a shape the scanner has not learned yet survives.
    auto parsed = json::Value::parse(document);
    if (!parsed) {
        return std::unexpected(StoreError::malformedDocument);
    }
    return decodeEntry(*parsed);
}

std::expected<Entry, StoreError> decodeEntry(json::ScalarScan &cursor, bool *scannerRefused)
{
    // The probe bounds the value before anything is committed, so the fallback
    // gets the value alone and the cursor skips what it read.
    json::ScalarScan probe = cursor;
    const std::size_t start = probe.offset();
    if (!probe.skipValue(0)) {
        return std::unexpected(StoreError::malformedDocument);
    }
    const std::string_view slice = probe.text().substr(start, probe.offset() - start);
    cursor.advanceTo(probe.offset());
    return decodeEntry(slice, scannerRefused);
}

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

    // Same rule as the scanner path: a list item has no `kind` of its own.
    const std::string_view kind = object.textAt("kind");
    if (kind.size() > TEXT_CEILING) {
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
