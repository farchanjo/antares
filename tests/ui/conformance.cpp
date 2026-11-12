// Conformance for the resource list model. One case per functional
// requirement: the model is a lens over the store, so every case asserts an
// operation the view would receive, never a row the model copied.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <csignal>
#include <cstdlib>
#include <cstring>

#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QTimer>
#include <unistd.h>

#include "adapters/ui/application.hpp"


#include "adapters/ui/column_width.hpp"
#include "adapters/ui/page_error.hpp"
#include "adapters/ui/resource_list_model.hpp"
#include "adapters/ui/row_role.hpp"
#include "adapters/ui/row_text.hpp"
#include "adapters/ui/view_config.hpp"
#include "domain/k8s/change_set.hpp"
#include "domain/k8s/entry.hpp"
#include "domain/k8s/store_budget.hpp"
#include "harness.hpp"

namespace {

using namespace antares::test::ui;
using antares::adapters::ui::ColumnWidth;
using antares::adapters::ui::PageError;
using antares::adapters::ui::ResourceListModel;
using antares::adapters::ui::RowRole;
using antares::adapters::ui::ViewConfig;
using RecordedOp = antares::test::ui::RecordedOp;

// FR-001 — the model holds no data and every row reads the store.
void caseModelFootprintUnder4k()
{
    Harness harness;
    harness.build();
    harness.seed(5000);

    check(harness.model->rowCount() == 5000, "every stored row is a row");
    check(harness.model->footprintBytes() <= 4096, "the model footprint stays under 4 KiB");
    check(harness.model->footprintBytes() == sizeof(ResourceListModel),
          "a row adds no member: the footprint is the object itself");
    checkEqual(harness.model->data(0, RowRole::name).value_or(""), nameFor(0),
               "the first row reads the store");
    checkEqual(harness.model->data(4999, RowRole::name).value_or(""), nameFor(4999),
               "the last row reads the store");
    check(harness.store.totalEntries() == 5000, "the store is the only copy");
    std::printf("  footprint: %zu bytes for 5000 rows\n", harness.model->footprintBytes());
}

// FR-003 — the four roles, filled in one call.
void caseMultiDataFillsSpan()
{
    Harness harness;
    harness.build();
    harness.seed(1);

    const std::array<RowRole, 4> roles{RowRole::name, RowRole::namespaceName, RowRole::status,
                                       RowRole::age};
    std::array<std::string, 4> texts;
    harness.model->multiData(0, roles, texts);

    checkEqual(texts[0], nameFor(0), "name role");
    checkEqual(texts[1], "shop", "namespace role reads the scope");
    checkEqual(texts[2], "Running", "status role");
    checkEqual(texts[3], "2026-11-11T19:05:00Z", "age role is the creation stamp");

    std::array<std::string, 2> shorter;
    const std::array<RowRole, 2> two{RowRole::name, RowRole::status};
    harness.model->multiData(0, two, shorter);
    checkEqual(shorter[0], nameFor(0), "a shorter span fills what it asked for");

    std::array<std::string, 1> past;
    harness.model->multiData(7, two, past);
    checkEqual(past[0], "", "a row past the end fills empty");
    check(!harness.model->data(7, RowRole::name).has_value(), "a row past the end has no data");
}

// The ready/total pair is the status when the server gave no reason.
void caseStatusTextFallsBack()
{
    Harness harness;
    harness.build();
    harness.seed(1);
    harness.store.find(harness.model->scope());

    Entry entry;
    entry.status.reason.clear();
    entry.status.ready = 1;
    entry.status.total = 3;
    checkEqual(antares::adapters::ui::rowText(entry, harness.model->scope(), RowRole::status), "1/3",
               "ready over total");

    entry.status.total.reset();
    checkEqual(antares::adapters::ui::rowText(entry, harness.model->scope(), RowRole::status), "",
               "half a pair renders nothing");
}

// FR-002 — a page is inserted as one range and asks for the documented limit.
void caseFetchInsertsOneRange()
{
    Harness harness;
    harness.build();
    harness.seed(2);
    harness.sink.clear();

    harness.pages.queuePage(harness.page(2, 3), "");
    harness.model->fetchMore();

    check(harness.sink.ops.size() == 1, "one operation per page");
    check(harness.sink.ops.at(0) == RecordedOp::Kind::inserted, "the operation is an insert");
    check(harness.sink.ops.at(0).first == 2 && harness.sink.ops.at(0).last == 4,
          "the whole page is one range");
    check(harness.model->rowCount() == 5, "the page added its rows");
    check(!harness.model->canFetchMore(), "an empty token ends the fetch");
}

void caseFetchAsksTheLimit()
{
    Harness harness;
    harness.build();
    harness.seed(1);
    harness.pages.queuePage(harness.page(1, 1), "next");

    harness.model->fetchMore();

    check(harness.pages.lastLimit == ResourceListModel::pageLimit, "the limit is 500");
    check(harness.pages.fetches == 1, "one page was asked for");
    checkEqual(harness.pages.lastToken, "start", "the token the first page left is the one asked with");
}

void caseCanFetchMoreNeedsAToken()
{
    Harness empty;
    empty.build("shop", "");
    check(!empty.model->canFetchMore(), "no token, no fetch");
    empty.model->fetchMore();
    check(empty.pages.fetches == 0, "fetchMore is a no-op without a token");

    Harness harness;
    harness.build();
    check(harness.model->canFetchMore(), "a token keeps the fetch open");

    harness.pages.queuePage(harness.page(0, 1), "");
    harness.model->fetchMore();
    check(!harness.model->canFetchMore(), "the last page ends the fetch");
}

// FR-004 — removals descend, insertions ascend, changes last.
void caseChangeSetAppliesInOrder()
{
    Harness harness;
    harness.build();
    harness.seed(5);
    harness.sink.clear();

    const auto removedFirst = entryFor(1);
    const auto removedSecond = entryFor(4);
    const auto added = entryFor(99);
    check(removedFirst && removedSecond && added, "fixtures build");
    if (!removedFirst || !removedSecond || !added) {
        return;
    }

    const std::array<WatchEvent, 3> batch{
        eventOf(EventType::deleted, *removedFirst),
        eventOf(EventType::deleted, *removedSecond),
        eventOf(EventType::added, *added),
    };
    static_cast<void>(harness.store.apply(harness.model->scope(), batch));

    check(harness.sink.ops.size() == 3, "three operations for three deltas");
    if (harness.sink.ops.size() != 3) {
        return;
    }
    check(harness.sink.ops.at(0) == RecordedOp::Kind::removed && harness.sink.ops.at(0).first == 4,
          "the higher removal comes first");
    check(harness.sink.ops.at(1) == RecordedOp::Kind::removed && harness.sink.ops.at(1).first == 1,
          "removals descend");
    check(harness.sink.ops.at(2) == RecordedOp::Kind::inserted && harness.sink.ops.at(2).first == 3,
          "the insertion is stated against the rows that remain");
    check(harness.model->rowCount() == 4, "two removals and one insert net out");
}

// An unrendered change repaints nothing.
void caseEmptyMaskRepaintsNothing()
{
    Harness harness;
    harness.build();
    harness.seed(1);
    harness.sink.clear();

    const auto same = entryFor(0);
    check(same.has_value(), "fixture builds");
    if (!same) {
        return;
    }
    const std::array<WatchEvent, 1> batch{eventOf(EventType::modified, *same)};
    static_cast<void>(harness.store.apply(harness.model->scope(), batch));

    check(harness.sink.ops.empty(), "an unchanged entry emits nothing");
}

// A status-only change repaints the status role and does not move the row.
void caseStatusChangeRepaintsStatus()
{
    Harness harness;
    harness.build();
    harness.seed(2);
    harness.sink.clear();

    auto changed = entryFor(0);
    check(changed.has_value(), "fixture builds");
    if (!changed) {
        return;
    }
    changed->status.reason = "CrashLoopBackOff";
    const std::array<WatchEvent, 1> batch{eventOf(EventType::modified, *changed)};
    static_cast<void>(harness.store.apply(harness.model->scope(), batch));

    check(harness.sink.ops.size() == 1, "one operation");
    if (harness.sink.ops.size() != 1) {
        return;
    }
    check(harness.sink.ops.at(0) == RecordedOp::Kind::changed, "it is a repaint");
    check(harness.sink.ops.at(0).roles.size() == 1 &&
              harness.sink.ops.at(0).roles.at(0) == RowRole::status,
          "only the status role is named");
    checkEqual(harness.model->data(0, RowRole::status).value_or(""), "CrashLoopBackOff",
               "the new status is read from the store");
}

// FR-005 — the ordering key is a move, and the destination follows the rule.
void caseReorderIsAMove()
{
    Harness harness;
    harness.build();
    harness.seed(3);
    harness.sink.clear();

    auto changed = entryFor(0);
    check(changed.has_value(), "fixture builds");
    if (!changed) {
        return;
    }
    changed->generation = 2;
    const std::array<WatchEvent, 1> batch{eventOf(EventType::modified, *changed)};
    static_cast<void>(harness.store.apply(harness.model->scope(), batch));

    check(harness.sink.count(RecordedOp::Kind::removed) == 0, "a move is not a removal");
    check(harness.sink.count(RecordedOp::Kind::inserted) == 0, "a move is not an insertion");
    check(harness.sink.count(RecordedOp::Kind::moved) == 1, "the row moved once");
    if (harness.sink.count(RecordedOp::Kind::moved) != 1) {
        return;
    }
    const RecordedOp &move = harness.sink.ops.back();
    check(move.first == 0 && move.last == 0, "the moved row is the changed one");
    check(move.destination == 3, "moving down, the destination is the target plus one");
    checkEqual(harness.model->data(2, RowRole::name).value_or(""), nameFor(0),
               "the row is last now");
    checkEqual(harness.model->data(0, RowRole::name).value_or(""), nameFor(1),
               "the rows above shifted up");
    check(harness.model->rowCount() == 3, "a move keeps the count");
}

void caseMoveOfLastRowIsSilent()
{
    Harness harness;
    harness.build();
    harness.seed(3);
    harness.sink.clear();

    auto changed = entryFor(2);
    check(changed.has_value(), "fixture builds");
    if (!changed) {
        return;
    }
    changed->generation = 2;
    const std::array<WatchEvent, 1> batch{eventOf(EventType::modified, *changed)};
    static_cast<void>(harness.store.apply(harness.model->scope(), batch));

    check(harness.sink.count(RecordedOp::Kind::moved) == 0, "the last row cannot move down");
    check(harness.sink.count(RecordedOp::Kind::changed) == 1, "it still repaints");
}

// FR-006, FR-007 — the view is batched, uniform and fixed.
void caseViewConfigIsBatchedAndFixed()
{
    const ViewConfig config;
    check(config.batched(), "layout is batched");
    check(config.batchSize() == ViewConfig::defaultBatchSize, "the default batch size is kept");
    check(config.uniformItemSizes(), "item sizes start uniform");
    check(config.headerResizeFixed(), "the header never measures contents");
    check(config.columns().size() == antares::adapters::ui::ROW_ROLE_COUNT, "every role has a column");
    for (const ColumnWidth &column : config.columns()) {
        check(column.width > 0, "a column has a width");
    }
}

void caseViewConfigTakesTheSavedLayout()
{
    const std::array<ColumnWidth, 4> saved{ColumnWidth{RowRole::name, 400},
                                           ColumnWidth{RowRole::namespaceName, 200},
                                           ColumnWidth{RowRole::status, 120},
                                           ColumnWidth{RowRole::age, 80}};
    const ViewConfig config{saved};
    check(config.columns().at(0).width == 400, "the saved width wins");
    check(config.columns().at(3).role == RowRole::age, "the saved order wins");

    const std::array<ColumnWidth, 2> partial{ColumnWidth{RowRole::name, 400},
                                             ColumnWidth{RowRole::namespaceName, 200}};
    const ViewConfig fallback{partial};
    check(fallback.columns().at(0).width == ViewConfig::defaultLayout.at(0).width,
          "a layout missing a role falls back");

    const std::array<ColumnWidth, 4> zero{ColumnWidth{RowRole::name, 0},
                                          ColumnWidth{RowRole::namespaceName, 200},
                                          ColumnWidth{RowRole::status, 120},
                                          ColumnWidth{RowRole::age, 80}};
    const ViewConfig zeroed{zero};
    check(zeroed.columns().at(0).width == ViewConfig::defaultLayout.at(0).width,
          "a zero width falls back");
}

// T007 — uniformity is conditional and never comes back.
void caseUniformFlagFlipsOnAVariedRow()
{
    ViewConfig config;
    check(config.uniformItemSizes(), "uniform to start");
    config.noteRowHeight(24);
    check(config.uniformItemSizes(), "the first height is only remembered");
    config.noteRowHeight(24);
    check(config.uniformItemSizes(), "the same height keeps the hint");
    config.noteRowHeight(40);
    check(!config.uniformItemSizes(), "a different height makes the hint a lie");
    config.noteRowHeight(24);
    check(!config.uniformItemSizes(), "and it stays off");
}

// FR-008 — a batch from another thread crosses as a value.
void caseCrossThreadChangeSetQueues()
{
    Harness harness;
    harness.build();
    harness.seed(1);
    harness.sink.clear();
    harness.wakeup.wakes = 0;

    antares::domain::k8s::ChangeSet changes;
    changes.removed.push_back(antares::domain::k8s::RowRange{0, 1});

    std::thread producer{[&harness, &changes] { harness.model->applyLater(std::move(changes)); }};
    producer.join();

    check(harness.wakeup.wakes == 1, "the model was woken once");
    check(harness.sink.count(RecordedOp::Kind::removed) == 1,
          "the queued batch was replayed on the model's thread");
}

// T009 — a failed fetch shows one retry row and does not retry by itself.
void caseFetchFailureShowsRetryRow()
{
    Harness harness;
    harness.build();
    harness.seed(2);
    harness.sink.clear();

    harness.pages.queueFailure(PageError::refused);
    harness.model->fetchMore();

    check(harness.model->rowCount() == 3, "the retry row is appended");
    checkEqual(harness.model->data(2, RowRole::name).value_or(""), "retry", "the row names retry");
    checkEqual(harness.model->data(2, RowRole::status).value_or(""), "refused",
               "the row shows the error family");
    check(!harness.model->canFetchMore(), "a failed page stops the view from asking again");
    check(harness.sink.count(RecordedOp::Kind::inserted) == 1, "one insert for the retry row");
    check(harness.logger.sawWarn("ui"), "the failure is logged at warn");
}

void caseRetryDoesNotLoop()
{
    Harness harness;
    harness.build();
    harness.seed(2);

    harness.pages.queueFailure(PageError::transport);
    harness.model->fetchMore();
    check(harness.pages.fetches == 1, "one fetch so far");

    for (int attempt = 0; attempt < 10; ++attempt) {
        static_cast<void>(harness.model->canFetchMore());
    }
    check(harness.pages.fetches == 1, "asking canFetchMore does not fetch");

    harness.pages.queueFailure(PageError::transport);
    harness.model->retry();
    check(harness.pages.fetches == 2, "the retry asks once");
    check(harness.model->rowCount() == 3, "one retry row, not two");
    check(!harness.model->canFetchMore(), "a second failure settles again");

    harness.sink.clear();
    harness.pages.queuePage(harness.page(2, 1), "");
    harness.model->retry();
    check(harness.pages.fetches == 3, "the successful retry asked once");
    check(harness.model->rowCount() == 3, "the retry row made room for the page row");
    check(harness.sink.count(RecordedOp::Kind::removed) == 1, "the retry row was removed");
    check(harness.sink.count(RecordedOp::Kind::inserted) == 1, "the page row was inserted");
    checkEqual(harness.model->data(2, RowRole::name).value_or(""), nameFor(2), "the page row");
}

// Truncation is reported from the scope, not guessed.
void caseTruncationIsReported()
{
    antares::domain::k8s::StoreBudget budget;
    budget.maxEntries = 2;
    Harness harness{budget};
    harness.build();
    harness.seed(3);

    check(harness.model->rowCount() == 2, "the ceiling admits two rows");
    check(harness.model->truncated(), "the scope reports truncation");
    check(harness.model->refused() == 1, "the refusal is counted");
}

// The role ids are the CUE contract.
void caseRoleIndexMatchesCue()
{
    check(antares::adapters::ui::roleIndex(RowRole::name) == 257, "name is 257");
    check(antares::adapters::ui::roleIndex(RowRole::namespaceName) == 258, "namespace is 258");
    check(antares::adapters::ui::roleIndex(RowRole::status) == 259, "status is 259");
    check(antares::adapters::ui::roleIndex(RowRole::age) == 260, "age is 260");
    for (const RowRole role :
         {RowRole::name, RowRole::namespaceName, RowRole::status, RowRole::age}) {
        const auto back = antares::adapters::ui::roleForIndex(antares::adapters::ui::roleIndex(role));
        check(back.has_value() && *back == role, "the id round-trips");
        check(!antares::adapters::ui::rowRoleName(role).empty(), "the role has a name");
    }
    check(!antares::adapters::ui::roleForIndex(999).has_value(), "an unregistered id names nothing");
}

// The mask maps onto roles and the ordering key is `generation`.
void caseRolesForMask()
{
    const std::array<antares::domain::k8s::EntryField, 1> generation{antares::domain::k8s::EntryField::generation};
    std::array<RowRole, antares::adapters::ui::ROW_ROLE_COUNT> scratch{};
    const auto roles = antares::adapters::ui::rolesFor(generation, scratch);
    check(roles.size() == 2, "a generation change repaints two roles");
    check(antares::adapters::ui::carriesOrderingKey(generation), "generation is the ordering key");

    const std::array<antares::domain::k8s::EntryField, 2> duplicate{antares::domain::k8s::EntryField::status,
                                                           antares::domain::k8s::EntryField::status};
    const auto once = antares::adapters::ui::rolesFor(duplicate, scratch);
    check(once.size() == 1, "a repeated field yields one role");
    check(!antares::adapters::ui::carriesOrderingKey(duplicate), "status is not the ordering key");

    const std::span<const antares::domain::k8s::EntryField> empty;
    check(antares::adapters::ui::rolesFor(empty, scratch).empty(), "an empty mask names no role");
}

struct Case {
    std::string_view name;
    void (*run)();
};

const std::array<Case, 20> cases{{
    {"model-footprint-under-4k", caseModelFootprintUnder4k},
    {"multi-data-fills-span", caseMultiDataFillsSpan},
    {"status-text-falls-back", caseStatusTextFallsBack},
    {"fetch-inserts-one-range", caseFetchInsertsOneRange},
    {"fetch-asks-the-limit", caseFetchAsksTheLimit},
    {"can-fetch-more-needs-a-token", caseCanFetchMoreNeedsAToken},
    {"change-set-applies-in-order", caseChangeSetAppliesInOrder},
    {"empty-mask-repaints-nothing", caseEmptyMaskRepaintsNothing},
    {"status-change-repaints-status", caseStatusChangeRepaintsStatus},
    {"reorder-is-a-move", caseReorderIsAMove},
    {"move-of-last-row-is-silent", caseMoveOfLastRowIsSilent},
    {"view-config-batched-and-fixed", caseViewConfigIsBatchedAndFixed},
    {"view-config-takes-saved-layout", caseViewConfigTakesTheSavedLayout},
    {"uniform-flag-flips-on-varied-row", caseUniformFlagFlipsOnAVariedRow},
    {"cross-thread-change-set-queues", caseCrossThreadChangeSetQueues},
    {"fetch-failure-shows-retry-row", caseFetchFailureShowsRetryRow},
    {"retry-does-not-loop", caseRetryDoesNotLoop},
    {"truncation-is-reported", caseTruncationIsReported},
    {"role-index-matches-cue", caseRoleIndexMatchesCue},
    {"roles-for-mask", caseRolesForMask},
}};

/// A kubeconfig good enough to resolve a binding without touching a cluster.
/// The server is never dialled at startup, so the name need not resolve.
constexpr std::string_view FIXTURE = R"(apiVersion: v1
kind: Config
current-context: dev
clusters:
- name: alpha
  cluster:
    server: https://alpha:6443
    insecure-skip-tls-verify: true
contexts:
- name: dev
  context:
    cluster: alpha
    user: builder
    namespace: work
users:
- name: builder
  user:
    token: s3cr3t
)";

/// Write the fixture to a temporary file and point `KUBECONFIG` at it.
[[nodiscard]] std::string installFixture()
{
    std::array<char, 64> path{};
    std::snprintf(path.data(), path.size(), "/tmp/antares-ui-XXXXXX");
    const int fd = ::mkstemp(path.data());
    if (fd < 0) {
        return {};
    }
    (void)::write(fd, FIXTURE.data(), FIXTURE.size());
    (void)::close(fd);
    (void)::setenv("KUBECONFIG", path.data(), 1);
    return {path.data()};
}

void removeFixture(const std::string &path)
{
    if (!path.empty()) {
        (void)::unlink(path.c_str());
    }
}

/// T008 — the window and both notifiers exist, and shutdown is the reverse of
/// construction with the engine last (FR-001, FR-004).
void caseBuildAndShutdown(QApplication &app)
{
    const std::string path = installFixture();
    check(!path.empty(), "fixture written");

    antares::adapters::ui::Application application{app};
    const auto built = application.build();
    check(built.has_value(), "build succeeds against the fixture");
    if (built) {
        check(application.window() != nullptr, "window exists");
        check(application.wakeupNotifier() != nullptr, "engine wakeup notifier installed");
        check(application.signalNotifier() != nullptr, "signal notifier installed");
        check(application.trace().empty(), "nothing destroyed before shutdown");
    }

    application.shutdown();

    const std::array<std::string_view, 6> expected{
        "window", "model", "store", "kube", "client", "engine"};
    check(application.trace().size() == expected.size(), "every step destroyed once");
    for (std::size_t index = 0; index < expected.size() && index < application.trace().size();
         ++index) {
        check(application.trace()[index] == expected[index], "destruction runs in reverse");
    }
    check(application.window() == nullptr, "window released");
    removeFixture(path);
}

/// T007 — two instances start without writing the kubeconfig (FR-007). The
/// loader opens it read-only, so the bytes must be identical afterwards.
void caseTwoInstances(QApplication &app)
{
    const std::string path = installFixture();
    check(!path.empty(), "fixture written");

    antares::adapters::ui::Application first{app};
    antares::adapters::ui::Application second{app};
    check(first.build().has_value(), "first instance builds");
    check(second.build().has_value(), "second instance builds");

    std::string after;
    if (std::FILE *file = std::fopen(path.c_str(), "rb")) {
        std::array<char, 1024> buffer{};
        const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), file);
        after.assign(buffer.data(), read);
        (void)std::fclose(file);
    }
    check(after == FIXTURE, "kubeconfig is untouched");

    second.shutdown();
    first.shutdown();
    removeFixture(path);
}

/// T009 — a raised `SIGTERM` reaches the self-pipe and `exec()` returns inside
/// the bound (FR-005).
void caseSigtermReturns(QApplication &app)
{
    const std::string path = installFixture();
    antares::adapters::ui::Application application{app};
    const auto built = application.build();
    check(built.has_value(), "build succeeds against the fixture");
    if (!built) {
        removeFixture(path);
        return;
    }

    // Raise from the loop, not from `main`: before `exec()` the handler would
    // write the pipe and the notifier would not run until the loop starts.
    QTimer::singleShot(0, [] { (void)::raise(SIGTERM); });

    QElapsedTimer clock;
    clock.start();
    const int code = app.exec();
    const qint64 elapsed = clock.elapsed();

    check(code == 0, "clean exit code");
    check(elapsed < 2000, "exec() returns inside the bound");

    application.shutdown();
    removeFixture(path);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::printf("usage: antares_ui_conformance <case>\n");
        return 2;
    }

    // A widget application is needed by the three window cases and is harmless
    // for the others, so it is created once here.
    QApplication app{argc, argv};
    const std::string_view wanted{argv[1]};

    std::printf("%.*s:\n", static_cast<int>(wanted.size()), wanted.data());
    if (wanted == "build-and-shutdown") {
        caseBuildAndShutdown(app);
    } else if (wanted == "two-instances") {
        caseTwoInstances(app);
    } else if (wanted == "sigterm-returns") {
        caseSigtermReturns(app);
    } else {
        bool known = false;
        for (const Case &entry : cases) {
            if (entry.name != wanted) {
                continue;
            }
            entry.run();
            known = true;
            break;
        }
        if (!known) {
            std::printf("unknown case '%.*s'\n", static_cast<int>(wanted.size()), wanted.data());
            return 2;
        }
    }

    std::printf("  %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
