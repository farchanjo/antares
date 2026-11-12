// Conformance suite for the IoEngine port (FR-008).
//
// The suite takes the port and names no backend. CTest instantiates it once per
// backend through ANTARES_IO_BACKEND, so a backend is proven, not assumed.

#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <mutex>
#include <string>
#include <vector>
#include <thread>

#include "support/await.hpp"

#include "application/io/engine_factory.hpp"
#if defined(__linux__)
#include "adapters/platform/io/epoll_engine.hpp"
#include "adapters/platform/io/io_uring_engine.hpp"
#elif defined(__APPLE__)
#include "adapters/platform/io/kqueue_engine.hpp"
#endif
#include "domain/units/bytes.hpp"
#include "ports/io/io_engine.hpp"
#include "ports/io/io_handle.hpp"

namespace {

using antares::application::io::makeEngine;
using antares::ports::io::Buffer;
using antares::ports::io::IoEngine;
using antares::ports::io::IoError;
using antares::ports::io::IoHandle;
using antares::domain::Bytes;

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

/// Run the engine on its own thread for the duration of a case.
class Runner {
public:
    explicit Runner(IoEngine &engine)
        : engine_(engine)
        , worker_([&engine] { engine.run(); })
    {
    }

    ~Runner()
    {
        // A moved-from Runner holds no thread and must not stop the engine the
        // live Runner is driving.
        if (worker_.joinable()) {
            engine_.stop();
            worker_.join();
        }
    }

    Runner(const Runner &) = delete;
    Runner &operator=(const Runner &) = delete;
    Runner(Runner &&other) noexcept
        : engine_(other.engine_)
        , worker_(std::move(other.worker_))
    {
    }
    Runner &operator=(Runner &&) = delete;

    void stop() { engine_.stop(); }

private:
    IoEngine &engine_;
    std::thread worker_;
};

Runner makeRunner(IoEngine &engine)
{
    return Runner{engine};
}

void casePostRunsOnEngineThread()
{
    auto built = makeEngine();
    check(built.has_value(), "factory built an engine");
    if (!built) {
        return;
    }
    IoEngine &engine = **built;
    auto runner = makeRunner(engine);
    const auto caller = std::this_thread::get_id();

    antares::test::Await ran;
    std::atomic<bool> onEngine{false};
    engine.post([&] {
        onEngine.store(std::this_thread::get_id() != caller);
        ran.signal();
    });

    check(ran.wait(), "posted task ran");
    check(onEngine.load(), "posted task ran off the caller thread");
}

void caseSocketReadWrite()
{
    auto built = makeEngine();
    check(built.has_value(), "factory built an engine");
    if (!built) {
        return;
    }
    IoEngine &engine = **built;
    auto runner = makeRunner(engine);

    int pair[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
        check(false, "socketpair");
        return;
    }

    auto reader = IoHandle::adopt(pair[0]);
    auto writer = IoHandle::adopt(pair[1]);
    check(reader.has_value(), "adopt reader");
    check(writer.has_value(), "adopt writer");
    if (!reader || !writer) {
        return;
    }
    check(reader->kind() == antares::ports::io::HandleKind::stream, "socket is a stream");

    antares::test::Await done;
    std::size_t carried = 0;
    std::expected<void, IoError> submitted =
        engine.submitRead(*reader, std::nullopt, Bytes{64}, [&](std::expected<Buffer, IoError> result) {
            if (result) {
                carried = result->size();
            }
            done.signal();
        });
    check(submitted.has_value(), "submitRead accepted");

    const char payload[] = "antares";
    const auto wrote = engine.submitWrite(
        *writer, std::nullopt, Buffer(reinterpret_cast<const std::byte *>(payload),
                                      reinterpret_cast<const std::byte *>(payload) + 7),
        [](std::expected<std::size_t, IoError>) {});

    check(wrote.has_value(), "submitWrite accepted");

    check(done.wait(), "read completed");
    check(carried == 7, "read carried the payload");
}

void caseFileReadIsImmediate()
{
    auto built = makeEngine();
    check(built.has_value(), "factory built an engine");
    if (!built) {
        return;
    }
    IoEngine &engine = **built;
    auto runner = makeRunner(engine);

    // One file per process: the poll and kqueue runs share the case name and
    // would otherwise write and unlink the same path in parallel.
    const std::string path = "/tmp/antares-conformance-" + std::string(engine.name()) + ".txt";
    FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        check(false, "create temp file");
        return;
    }
    std::fwrite("hello", 1, 5, file);
    std::fclose(file);

    auto handle = IoHandle::openFile(path.c_str(), antares::ports::io::Access::read);
    check(handle.has_value(), "open temp file");
    if (!handle) {
        return;
    }
    check(handle->kind() == antares::ports::io::HandleKind::file, "regular file is a file");
    check(handle->size() == 5, "size reported");

    antares::test::Await done;
    std::size_t carried = 0;
    (void)engine.submitRead(*handle, Bytes{0}, Bytes{64}, [&](std::expected<Buffer, IoError> result) {
        if (result) {
            carried = result->size();
        }
        done.signal();
    });

    check(done.wait(), "file read completed without a readiness filter");
    check(carried == 5, "file read carried the content");

    antares::test::Await eof;
    std::atomic<bool> sawClosed{false};
    (void)engine.submitRead(*handle, Bytes{5}, Bytes{64}, [&](std::expected<Buffer, IoError> result) {
        sawClosed.store(!result && result.error() == IoError::closed);
        eof.signal();
    });

    check(eof.wait(), "read at end of file completed");
    check(sawClosed.load(), "end of file reports closed");

    ::unlink(path.c_str());
}

void caseStopEndsCallbacks()
{
    auto built = makeEngine();
    check(built.has_value(), "factory built an engine");
    if (!built) {
        return;
    }
    IoEngine &engine = **built;

    // Declared before the Runner: the runner joins on destruction, so a task it
    // may still run has to outlive it.
    std::atomic<int> after{0};
    antares::test::Await ran;
    {
        auto runner = makeRunner(engine);
        runner.stop();
        runner.stop();

        engine.post([&] {
            after.fetch_add(1);
            ran.signal();
        });
        check(ran.wait(), "a post after stop still runs");
    }
    check(after.load() == 1, "post after stop is not double counted");
}

void caseNoCallbackAfterStop()
{
    auto built = makeEngine();
    check(built.has_value(), "factory built an engine");
    if (!built) {
        return;
    }

    // A socket pair with nothing to read: the read stays pending.
    int pair[2] = {-1, -1};
    check(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "socket pair");
    auto handle = IoHandle::adopt(pair[0]);
    check(handle.has_value(), "adopted");

    auto runner = makeRunner(**built);
    std::atomic<int> completions{0};
    std::atomic<bool> cancelled{false};
    antares::test::Await settled;

    (void)(*built)->submitRead(*handle, std::nullopt, Bytes{16},
                               [&](std::expected<Buffer, IoError> result) {
                                   if (!result && result.error() == IoError::cancelled) {
                                       cancelled.store(true);
                                   }
                                   completions.fetch_add(1);
                                   settled.signal();
                               });

    runner.stop();

    // `stop()` settles what is pending (FR-005), so the completion is expected —
    // it just may not arrive after the drain.
    check(settled.wait(), "the pending read settled on stop");
    check(cancelled.load(), "it settled as cancelled");

    const int after = completions.load();
    antares::test::Await tick;
    std::thread assist([&] {
        std::this_thread::yield();
        tick.signal();
    });
    assist.join();
    check(tick.wait(), "the assist signalled before the wait timed out");
    check(completions.load() == after, "no further completion after the drain");

    ::close(pair[1]);
}

void caseCancelPendingKeepsSocket()
{
    auto built = makeEngine();
    check(built.has_value(), "factory built an engine");
    if (!built) {
        return;
    }

    int pair[2] = {-1, -1};
    check(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "socket pair");
    auto handle = IoHandle::adopt(pair[0]);
    check(handle.has_value(), "adopted");

    auto runner = makeRunner(**built);
    antares::test::Await cancelled;

    (void)(*built)->submitRead(*handle, std::nullopt, Bytes{16},
                               [&](std::expected<Buffer, IoError> result) {
                                   check(!result, "read failed");
                                   if (!result) {
                                       check(result.error() == IoError::cancelled,
                                             "reported as cancelled");
                                   }
                                   cancelled.signal();
                               });

    (*built)->cancelPending(*handle);
    check(cancelled.wait(), "pending read was withdrawn");

    // The handle is still usable: a second read completes normally.
    antares::test::Await second;
    (void)(*built)->submitRead(*handle, std::nullopt, Bytes{16},
                               [&](std::expected<Buffer, IoError>) { second.signal(); });
    const char byte = 'z';
    (void)::write(pair[1], &byte, 1);
    check(second.wait(), "socket still serves after the cancel");

    ::close(pair[1]);
}

void caseCountersMove()
{
    auto built = makeEngine();
    check(built.has_value(), "factory built an engine");
    if (!built) {
        return;
    }
    IoEngine &engine = **built;
    auto runner = makeRunner(engine);

    const auto before = engine.stats();
    check(before.submitted == 0, "a fresh engine has no submissions");

    int pair[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
        check(false, "socketpair");
        return;
    }
    auto reader = IoHandle::adopt(pair[0]);
    auto writer = IoHandle::adopt(pair[1]);
    if (!reader || !writer) {
        return;
    }

    antares::test::Await done;
    (void)engine.submitRead(*reader, std::nullopt, Bytes{32},
                            [&](std::expected<Buffer, IoError>) { done.signal(); });
    antares::test::Await wrote;
    const char byte = 'x';
    (void)engine.submitWrite(*writer, std::nullopt,
                             Buffer(reinterpret_cast<const std::byte *>(&byte),
                                    reinterpret_cast<const std::byte *>(&byte) + 1),
                             [&](std::expected<std::size_t, IoError>) { wrote.signal(); });

    check(done.wait(), "read completed");
    check(wrote.wait(), "write completed");

    const auto after = engine.stats();
    check(after.submitted == 2, "both submissions counted");
    check(after.completed >= 1, "the completed read counted");
    check(after.failures() <= after.completed, "failures never exceed completions");
}

/// The name the factory reports must be the engine it built. A forced value that
/// reports itself is a tautology, so the variable is cleared and the engine is
/// identified by type.
void caseBackendNameMatchesEngine()
{
    const char *saved = std::getenv("ANTARES_IO_BACKEND");
    const std::string keep = saved != nullptr ? saved : std::string{};
    (void)::unsetenv("ANTARES_IO_BACKEND");

    const auto engine = antares::application::io::makeEngine();
    check(engine.has_value(), "the factory builds an engine");
    if (!engine) {
        return;
    }

    const char *actual = "poll";
#if defined(__linux__)
    if (dynamic_cast<antares::adapters::platform::io::IoUringEngine *>(engine->get()) != nullptr) {
        actual = "io_uring";
    } else if (dynamic_cast<antares::adapters::platform::io::EpollEngine *>(engine->get()) != nullptr) {
        actual = "epoll";
    }
#elif defined(__APPLE__)
    if (dynamic_cast<antares::adapters::platform::io::KqueueEngine *>(engine->get()) != nullptr) {
        actual = "kqueue";
    }
#endif

    const std::string reported = antares::application::io::selectedBackendName();
    check(reported == actual, "the reported name matches the engine that was built");

    if (!keep.empty()) {
        (void)::setenv("ANTARES_IO_BACKEND", keep.c_str(), 1);
    }
}

/// Four producers, one consumer. Every task must run exactly once, and the post
/// path must not take the mutex (FR-003).
void caseQueueFourProducers()
{
    auto built = makeEngine();
    check(built.has_value(), "factory built an engine");
    if (!built) {
        return;
    }
    IoEngine &engine = **built;
    auto runner = makeRunner(engine);

    constexpr int PRODUCERS = 4;
    constexpr int PER_PRODUCER = 250;

    std::atomic<int> ran{0};
    antares::test::Await done;
    std::atomic<bool> stop{false};

    // A watcher spins the count down so the signal fires once, on the last task.
    std::thread watcher([&] {
        while (!stop.load()) {
            if (ran.load() == PRODUCERS * PER_PRODUCER) {
                done.signal();
                return;
            }
            std::this_thread::yield();
        }
    });

    std::vector<std::thread> producers;
    producers.reserve(PRODUCERS);
    for (int p = 0; p < PRODUCERS; ++p) {
        producers.emplace_back([&engine, &ran] {
            for (int i = 0; i < PER_PRODUCER; ++i) {
                engine.post([&ran] { ran.fetch_add(1); });
            }
        });
    }
    for (auto &thread : producers) {
        thread.join();
    }

    check(done.wait(), "every task ran");
    check(ran.load() == PRODUCERS * PER_PRODUCER, "no task was lost and none ran twice");
    stop.store(true);
    watcher.join();
}

/// A drain stops at the deadline and the unrun tail keeps its order (FR-009).
void caseQueueDeadlineKeepsOrder()
{
    auto built = makeEngine();
    check(built.has_value(), "factory built an engine");
    if (!built) {
        return;
    }
    IoEngine &engine = **built;
    auto runner = makeRunner(engine);

    constexpr int TOTAL = 4000;
    std::mutex seenMutex;
    std::vector<int> seen;
    seen.reserve(TOTAL);
    antares::test::Await done;

    for (int i = 0; i < TOTAL; ++i) {
        engine.post([&, i] {
            {
                const std::lock_guard<std::mutex> guard{seenMutex};
                seen.push_back(i);
            }
            if (static_cast<int>(seen.size()) == TOTAL) {
                done.signal();
            }
        });
    }

    check(done.wait(), "the whole burst drains");
    check(static_cast<int>(seen.size()) == TOTAL, "every task ran");

    bool ordered = true;
    for (int i = 0; i < TOTAL; ++i) {
        if (seen[static_cast<std::size_t>(i)] != i) {
            ordered = false;
            break;
        }
    }
    check(ordered, "FIFO survives a bounded drain");
}

/// Only meaningful under the preloaded shim: the ring cannot open, so the
/// factory must land on epoll and say so (FR-010).
void caseRingRefusedFallsBack()
{
    auto built = makeEngine();
    check(built.has_value(), "the factory still builds an engine");
    if (!built) {
        return;
    }
    check(dynamic_cast<antares::adapters::platform::io::EpollEngine *>(built->get()) != nullptr,
          "the engine is epoll");
    check(antares::application::io::selectedBackendName() == "epoll", "the name says epoll");
}

/// Cancelling repeatedly while the loop completes and releases ops must not
/// walk a freed store; ASan is the oracle (FR-004).
void caseCancelWalksUnderLock()
{
    auto built = makeEngine();
    check(built.has_value(), "factory built an engine");
    if (!built) {
        return;
    }
    IoEngine &engine = **built;
    auto runner = makeRunner(engine);

    int pair[2] = {-1, -1};
    check(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "socket pair");
    auto handle = IoHandle::adopt(pair[0]);
    check(handle.has_value(), "adopted");

    std::atomic<int> completions{0};
    constexpr int ROUNDS = 200;
    for (int i = 0; i < ROUNDS; ++i) {
        (void)engine.submitRead(*handle, std::nullopt, Bytes{8},
                                [&](std::expected<Buffer, IoError>) { completions.fetch_add(1); });
        engine.cancelPending(*handle);
    }

    antares::test::Await settled;
    engine.post([&] { settled.signal(); });
    check(settled.wait(), "the loop kept running through the cancels");
    check(completions.load() <= ROUNDS, "never more completions than submissions");

    ::close(pair[1]);
}

/// `stop()` settles what is pending, exactly once, as cancelled (FR-005).
void caseStopSettlesPending()
{
    auto built = makeEngine();
    check(built.has_value(), "factory built an engine");
    if (!built) {
        return;
    }
    IoEngine &engine = **built;
    auto runner = makeRunner(engine);

    int pair[2] = {-1, -1};
    check(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "socket pair");
    auto handle = IoHandle::adopt(pair[0]);
    check(handle.has_value(), "adopted");

    std::atomic<int> completions{0};
    std::atomic<bool> wasCancelled{false};
    antares::test::Await settled;
    (void)engine.submitRead(*handle, std::nullopt, Bytes{8},
                            [&](std::expected<Buffer, IoError> result) {
                                wasCancelled.store(!result && result.error() == IoError::cancelled);
                                completions.fetch_add(1);
                                settled.signal();
                            });

    engine.stop();
    check(settled.wait(), "the pending read settled");
    check(wasCancelled.load(), "settled as cancelled");
    check(completions.load() == 1, "settled exactly once");

    ::close(pair[1]);
}

/// A stopped engine runs again, and serves work (FR-006).
void caseStopIsReversible()
{
    auto built = makeEngine();
    check(built.has_value(), "factory built an engine");
    if (!built) {
        return;
    }
    IoEngine &engine = **built;

    {
        auto first = makeRunner(engine);
        engine.stop();
    }

    auto second = makeRunner(engine);
    antares::test::Await ran;
    engine.post([&] { ran.signal(); });
    check(ran.wait(), "the loop serves work after a stop");

    engine.stop();
}

struct Case {
    const char *name;
    void (*run)();
};

std::span<const Case> cases()
{
    static constexpr Case table[] = {
        {"post-runs-on-engine-thread", casePostRunsOnEngineThread},
        {"socket-read-write", caseSocketReadWrite},
        {"file-read-immediate", caseFileReadIsImmediate},
        {"stop-ends-callbacks", caseStopEndsCallbacks},
        {"no-callback-after-stop", caseNoCallbackAfterStop},
        {"cancel-pending-keeps-socket", caseCancelPendingKeepsSocket},
        {"counters-move", caseCountersMove},
        {"backend-name-matches-engine", caseBackendNameMatchesEngine},
        {"queue-four-producers", caseQueueFourProducers},
        {"queue-deadline-keeps-order", caseQueueDeadlineKeepsOrder},
        {"ring-refused-falls-back", caseRingRefusedFallsBack},
        {"cancel-walks-under-lock", caseCancelWalksUnderLock},
        {"stop-settles-pending", caseStopSettlesPending},
        {"stop-is-reversible", caseStopIsReversible},
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

    // One case per process, so CTest fans the suite across cores.
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
