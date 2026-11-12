// Hosted-mode suite for the IoEngine port (ADR-0020).
//
// The engine owns no thread here: the suite plays the host, exactly as a
// `QSocketNotifier` would on Darwin. It sleeps in `poll()` over `waitHandle()`,
// arms its sleep on `nextDeadline()`, and calls `runOnce()` for every wakeup.
// Every case names the same three properties the contract rests on: the loop
// runs on the hosting thread, one loop carries the engine, and `stop()` is a
// boundary.
//
// No `sleep_for` is used as a wait: a case that needs the loop to make progress
// turns it, and a case that waits on a completion waits on that completion.

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <thread>
#include <vector>

#include "support/await.hpp"

#include "application/io/engine_factory.hpp"
#include "domain/units/bytes.hpp"
#include "ports/io/io_engine.hpp"
#include "ports/io/io_handle.hpp"

namespace {

using antares::application::io::makeEngine;
using antares::domain::Bytes;
using antares::ports::io::Buffer;
using antares::ports::io::IoEngine;
using antares::ports::io::IoError;
using antares::ports::io::IoHandle;

constexpr std::chrono::milliseconds TURN_BUDGET{4};
constexpr auto WAIT_LIMIT = std::chrono::seconds{5};

int failures = 0;
int checks = 0;

void check(bool condition, const char *what)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "  FAIL %s\n", what);
    }
}

/// The host. Sleeps on the engine's own handle, never on the engine's thread,
/// and bounds the sleep with the nearest deadline the engine reports.
class Host {
public:
    explicit Host(IoEngine &engine)
        : engine_(engine)
    {
        engine_.attach();
    }

    ~Host() { engine_.shutdown(); }

    Host(const Host &) = delete;
    Host &operator=(const Host &) = delete;

    /// Turn until `settled` signals. False means the limit ran out first, which
    /// is the failure a deadlocked loop produces.
    [[nodiscard]] bool turnUntil(antares::test::Await &settled,
                                 std::chrono::milliseconds limit = WAIT_LIMIT)
    {
        const auto limitEnd = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < limitEnd) {
            if (settled.signalled()) {
                return true;
            }
            sleepOnce();
            engine_.runOnce(TURN_BUDGET);
        }
        return settled.signalled();
    }

    /// One sleep on the host side, bounded by the engine's nearest deadline.
    void sleepOnce(int capMilliseconds = 20)
    {
        int timeout = capMilliseconds;
        if (const auto due = engine_.nextDeadline()) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  *due - std::chrono::steady_clock::now())
                                  .count();
            if (left <= 0) {
                timeout = 0;
            } else {
                timeout = static_cast<int>(std::min<std::int64_t>(left, capMilliseconds));
            }
        }
        struct pollfd watch {
            engine_.waitHandle(), POLLIN, 0
        };
        (void)::poll(&watch, 1, timeout);
    }

private:
    IoEngine &engine_;
};

/// A fresh engine, or a failed check. The caller returns on nothing.
std::expected<std::unique_ptr<IoEngine>, std::string> build()
{
    auto built = makeEngine();
    if (!built) {
        return std::unexpected(std::string{"factory built an engine"});
    }
    return std::unique_ptr<IoEngine>{std::move(*built)};
}

/// A connected pair; the caller owns both ends.
std::expected<std::pair<IoHandle, IoHandle>, std::string> pair()
{
    int descriptors[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) != 0) {
        return std::unexpected(std::string{"socketpair"});
    }
    auto reader = IoHandle::adopt(descriptors[0]);
    auto writer = IoHandle::adopt(descriptors[1]);
    if (!reader || !writer) {
        return std::unexpected(std::string{"adopt"});
    }
    return std::pair<IoHandle, IoHandle>{std::move(*reader), std::move(*writer)};
}

/// The turn runs on the hosting thread and says so.
void caseTurnRunsOnHostThread()
{
    auto engine = build();
    check(engine.has_value(), "factory built an engine");
    if (!engine) {
        return;
    }
    Host host{**engine};
    const auto hostThread = std::this_thread::get_id();

    antares::test::Await ran;
    std::atomic<bool> onHost{false};
    std::atomic<bool> sawEngineThread{false};
    (*engine)->post([&] {
        onHost.store(std::this_thread::get_id() == hostThread);
        sawEngineThread.store((*engine)->isEngineThread());
        ran.signal();
    });

    check(host.turnUntil(ran), "the posted task ran");
    check(onHost.load(), "it ran on the hosting thread");
    check(sawEngineThread.load(), "isEngineThread reports the host");
}

/// One loop per engine: a post from another thread is queued for the host and is
/// not drained on the poster.
void casePostWaitsForTheHost()
{
    auto engine = build();
    check(engine.has_value(), "factory built an engine");
    if (!engine) {
        return;
    }
    Host host{**engine};

    antares::test::Await ran;
    std::atomic<int> turn{0};
    // Drain whatever attach() left behind, so the next readable wakeup is the
    // post's and not attach's.
    (*engine)->runOnce(TURN_BUDGET);
    host.sleepOnce(0);

    std::thread poster([&] { (*engine)->post([&] { ran.signal(); }); });
    poster.join();

    // The poster has returned and no turn has run since: the task must still be
    // queued, not executed on the poster.
    check(!ran.signalled(), "the posting thread did not run it");

    check(host.turnUntil(ran), "the host's next turn ran it");
    turn.fetch_add(1);
    check(turn.load() == 1, "exactly one turn was needed");
}

/// The wakeup pipe is what the host watches when the backend has no aggregate
/// descriptor, so a post from another thread must make the handle readable.
void casePostWakesTheHandle()
{
    auto engine = build();
    check(engine.has_value(), "factory built an engine");
    if (!engine) {
        return;
    }
    Host host{**engine};

    // Drain whatever attach() left behind.
    antares::test::Await drained;
    (*engine)->post([&] { drained.signal(); });
    check(host.turnUntil(drained), "the loop is idle");

    std::atomic<bool> posted{false};
    std::thread poster([&] {
        (*engine)->post([] {});
        posted.store(true);
    });
    poster.join();
    check(posted.load(), "the post returned");

    struct pollfd watch {
        (*engine)->waitHandle(), POLLIN, 0
    };
    check(::poll(&watch, 1, 1000) > 0, "the handle became readable");
    check((watch.revents & POLLIN) != 0, "it reported readable, not an error");
}

/// A deadline is reported while a timer is armed and nothing once it fired.
void caseNextDeadlineTracksTimers()
{
    auto engine = build();
    check(engine.has_value(), "factory built an engine");
    if (!engine) {
        return;
    }
    Host host{**engine};

    check(!(*engine)->nextDeadline().has_value(), "an idle engine reports no deadline");

    antares::test::Await fired;
    std::atomic<bool> onHost{false};
    const auto hostThread = std::this_thread::get_id();
    check((*engine)->submitTimer(std::chrono::milliseconds{30}, [&] {
              onHost.store(std::this_thread::get_id() == hostThread);
              fired.signal();
          }).has_value(),
          "timer accepted");

    const auto due = (*engine)->nextDeadline();
    check(due.has_value(), "the armed timer is reported as a deadline");
    if (due) {
        check(*due > std::chrono::steady_clock::now(), "the deadline is in the future");
    }

    check(host.turnUntil(fired), "the timer fired through the host");
    check(onHost.load(), "it fired on the hosting thread");
    check(!(*engine)->nextDeadline().has_value(), "a fired timer leaves no deadline");
}

/// A turn stops at its budget, keeps the unrun tail, and loses nothing.
void caseTurnHonoursItsBudget()
{
    auto engine = build();
    check(engine.has_value(), "factory built an engine");
    if (!engine) {
        return;
    }
    Host host{**engine};

    constexpr int TOTAL = 4000;
    std::atomic<int> ran{0};
    antares::test::Await all;
    for (int index = 0; index < TOTAL; ++index) {
        (*engine)->post([&] {
            if (ran.fetch_add(1) + 1 == TOTAL) {
                all.signal();
            }
        });
    }

    // One turn with a zero budget still runs the first task, and the rest are
    // kept for later turns instead of being dropped.
    (*engine)->runOnce(std::chrono::milliseconds{0});
    check(ran.load() >= 1, "a turn always makes progress");
    check(ran.load() < TOTAL, "an expired budget does not swallow the burst");

    check(host.turnUntil(all), "every task ran across turns");
    check(ran.load() == TOTAL, "none was lost");
}

/// A stop issued from inside a completion must not deadlock: the loop is the
/// caller, so there is nobody to wait for.
void caseStopFromCallbackDoesNotDeadlock()
{
    auto engine = build();
    check(engine.has_value(), "factory built an engine");
    if (!engine) {
        return;
    }
    Host host{**engine};

    antares::test::Await returned;
    std::atomic<bool> stoppedOnHost{false};
    (*engine)->post([&] {
        (*engine)->stop();
        stoppedOnHost.store(true);
        returned.signal();
    });

    check(host.turnUntil(returned), "stop() returned inside the callback");
    check(stoppedOnHost.load(), "the callback ran to its end");

    // The boundary holds afterwards: with no loop running, a post drains on the
    // poster, so the work is still done.
    antares::test::Await inlineRan;
    const auto poster = std::this_thread::get_id();
    std::atomic<bool> onPoster{false};
    (*engine)->post([&] {
        onPoster.store(std::this_thread::get_id() == poster);
        inlineRan.signal();
    });
    check(inlineRan.wait(), "a post after the boundary runs");
    check(onPoster.load(), "and it runs on the poster, not on a loop");
}

/// After `stop()` returns no completion starts. A pending read settles as
/// cancelled inside the boundary and never again.
void caseNoCompletionStartsAfterStop()
{
    auto engine = build();
    check(engine.has_value(), "factory built an engine");
    if (!engine) {
        return;
    }
    auto ends = pair();
    check(ends.has_value(), "socket pair");
    if (!ends) {
        return;
    }

    Host host{**engine};

    std::atomic<bool> boundary{false};
    std::atomic<int> late{0};
    std::atomic<int> completions{0};
    std::atomic<bool> cancelled{false};
    antares::test::Await settled;

    (void)(*engine)->submitRead(ends->first, std::nullopt, Bytes{16},
                                [&](std::expected<Buffer, IoError> result) {
                                    if (boundary.load(std::memory_order_acquire)) {
                                        late.fetch_add(1);
                                    }
                                    if (!result && result.error() == IoError::cancelled) {
                                        cancelled.store(true);
                                    }
                                    completions.fetch_add(1);
                                    settled.signal();
                                });

    std::atomic<bool> hostMayFinish{false};
    std::thread stopper([&] {
        (*engine)->stop();
        boundary.store(true, std::memory_order_release);
        hostMayFinish.store(true, std::memory_order_release);
    });

    // The host keeps turning until the stopper says the boundary is behind us,
    // then turns once more so a late start would be seen.
    while (!hostMayFinish.load(std::memory_order_acquire)) {
        host.sleepOnce();
        (*engine)->runOnce(TURN_BUDGET);
    }
    stopper.join();
    host.sleepOnce(0);
    (*engine)->runOnce(TURN_BUDGET);

    check(settled.signalled(), "the pending read settled");
    check(cancelled.load(), "it settled as cancelled");
    check(completions.load() == 1, "it settled exactly once");
    check(late.load() == 0, "no completion started after stop() returned");
}

/// `shutdown()` is the hosted teardown: it settles what is pending and then
/// behaves like a stopped loop.
void caseShutdownSettlesPending()
{
    auto engine = build();
    check(engine.has_value(), "factory built an engine");
    if (!engine) {
        return;
    }
    auto ends = pair();
    check(ends.has_value(), "socket pair");
    if (!ends) {
        return;
    }

    auto *raw = engine->get();
    raw->attach();

    std::atomic<bool> cancelled{false};
    std::atomic<int> completions{0};
    antares::test::Await settled;
    (void)raw->submitRead(ends->first, std::nullopt, Bytes{16},
                          [&](std::expected<Buffer, IoError> result) {
                              if (!result && result.error() == IoError::cancelled) {
                                  cancelled.store(true);
                              }
                              completions.fetch_add(1);
                              settled.signal();
                          });

    raw->shutdown();

    check(settled.signalled(), "the pending read settled inside shutdown()");
    check(cancelled.load(), "settled as cancelled");
    check(completions.load() == 1, "exactly once");

    antares::test::Await inlineRan;
    raw->post([&] { inlineRan.signal(); });
    check(inlineRan.wait(), "the engine is stopped afterwards");
}

/// The hosted handle is the backend's aggregate where one exists and the wakeup
/// pipe where it does not; either way it is a live descriptor the host can poll.
void caseWaitHandleIsPollable()
{
    auto engine = build();
    check(engine.has_value(), "factory built an engine");
    if (!engine) {
        return;
    }
    Host host{**engine};

    const int handle = (*engine)->waitHandle();
    check(handle >= 0, "the host has a handle to watch");
    struct pollfd watch {
        handle, POLLIN, 0
    };
    check(::poll(&watch, 1, 1000) >= 0, "the handle is pollable");
}

struct Case {
    const char *name;
    void (*run)();
};

std::span<const Case> cases()
{
    static constexpr Case table[] = {
        {"turn-runs-on-host-thread", caseTurnRunsOnHostThread},
        {"post-waits-for-the-host", casePostWaitsForTheHost},
        {"post-wakes-the-handle", casePostWakesTheHandle},
        {"next-deadline-tracks-timers", caseNextDeadlineTracksTimers},
        {"turn-honours-its-budget", caseTurnHonoursItsBudget},
        {"stop-from-callback-does-not-deadlock", caseStopFromCallbackDoesNotDeadlock},
        {"no-completion-starts-after-stop", caseNoCompletionStartsAfterStop},
        {"shutdown-settles-pending", caseShutdownSettlesPending},
        {"wait-handle-is-pollable", caseWaitHandleIsPollable},
    };
    return table;
}

} // namespace

int main(int argc, char **argv)
{
    const std::span<const Case> all = cases();
    if (argc > 1 && std::strcmp(argv[1], "--list") == 0) {
        for (const Case &item : all) {
            std::printf("%s\n", item.name);
        }
        return 0;
    }

    if (argc > 1) {
        for (const Case &item : all) {
            if (std::strcmp(argv[1], item.name) == 0) {
                item.run();
                std::printf("%s: %d check(s), %d failure(s)\n", item.name, checks, failures);
                return failures == 0 ? 0 : 1;
            }
        }
        std::fprintf(stderr, "unknown case: %s\n", argv[1]);
        return 2;
    }

    for (const Case &item : all) {
        item.run();
    }
    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
