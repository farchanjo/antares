// Conformance suite for the IoEngine port (FR-008).
//
// The suite takes the port and names no backend. CTest instantiates it once per
// backend through ANTARES_IO_BACKEND, so a backend is proven, not assumed.

#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <expected>
#include <string>
#include <thread>

#include "support/await.hpp"

#include "application/io/engine_factory.hpp"
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
    auto runner = makeRunner(engine);
    runner.stop();
    runner.stop();

    std::atomic<int> after{0};
    engine.post([&] { after.fetch_add(1); });
    check(after.load() <= 1, "post after stop is not double counted");
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
    std::atomic<bool> late{false};
    bool stopped = false;

    (void)(*built)->submitRead(*handle, std::nullopt, Bytes{16},
                               [&](std::expected<Buffer, IoError>) {
                                   ++completions;
                                   if (stopped) {
                                       late.store(true);
                                   }
                               });

    runner.stop();
    stopped = true;

    // The callback runs on the loop thread; once stopped, none may start.
    const int settled = completions.load();
    antares::test::Await tick;
    std::thread assist([&] {
        std::this_thread::yield();
        tick.signal();
    });
    assist.join();
    check(tick.wait(), "the assist signalled before the wait timed out");

    check(completions.load() == settled, "no completion after stop returned");
    check(!late.load(), "nothing arrived late");

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
