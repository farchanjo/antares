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
#include <thread>

#include "application/io/engine_factory.hpp"
#include "ports/io/io_engine.hpp"
#include "ports/io/io_handle.hpp"

namespace {

using antares::application::io::makeEngine;
using antares::ports::io::Buffer;
using antares::ports::io::IoEngine;
using antares::ports::io::IoError;
using antares::ports::io::IoHandle;

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

void casePostRunsOnEngineThread(IoEngine &engine)
{
    auto runner = makeRunner(engine);
    const auto caller = std::this_thread::get_id();

    std::atomic<bool> ran{false};
    std::atomic<bool> onEngine{false};
    engine.post([&] {
        onEngine.store(std::this_thread::get_id() != caller);
        ran.store(true);
    });

    for (int spin = 0; spin < 400 && !ran.load(); ++spin) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(ran.load(), "posted task ran");
    check(onEngine.load(), "posted task ran off the caller thread");
}

void caseSocketReadWrite(IoEngine &engine)
{
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

    std::atomic<bool> done{false};
    std::size_t carried = 0;
    std::expected<void, IoError> submitted =
        engine.submitRead(*reader, std::nullopt, 64, [&](std::expected<Buffer, IoError> result) {
            if (result) {
                carried = result->size();
            }
            done.store(true);
        });
    check(submitted.has_value(), "submitRead accepted");

    const char payload[] = "antares";
    const auto wrote = engine.submitWrite(
        *writer, std::nullopt, Buffer(reinterpret_cast<const std::byte *>(payload),
                                      reinterpret_cast<const std::byte *>(payload) + 7),
        [](std::expected<std::size_t, IoError>) {});

    check(wrote.has_value(), "submitWrite accepted");

    for (int spin = 0; spin < 400 && !done.load(); ++spin) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(done.load(), "read completed");
    check(carried == 7, "read carried the payload");
}

void caseFileReadIsImmediate(IoEngine &engine)
{
    auto runner = makeRunner(engine);

    const char *path = "/tmp/antares-conformance.txt";
    FILE *file = std::fopen(path, "wb");
    if (file == nullptr) {
        check(false, "create temp file");
        return;
    }
    std::fwrite("hello", 1, 5, file);
    std::fclose(file);

    auto handle = IoHandle::openFile(path, antares::ports::io::Access::read);
    check(handle.has_value(), "open temp file");
    if (!handle) {
        return;
    }
    check(handle->kind() == antares::ports::io::HandleKind::file, "regular file is a file");
    check(handle->size() == 5, "size reported");

    std::atomic<bool> done{false};
    std::size_t carried = 0;
    (void)engine.submitRead(*handle, 0, 64, [&](std::expected<Buffer, IoError> result) {
        if (result) {
            carried = result->size();
        }
        done.store(true);
    });

    for (int spin = 0; spin < 400 && !done.load(); ++spin) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(done.load(), "file read completed without a readiness filter");
    check(carried == 5, "file read carried the content");

    std::atomic<bool> eof{false};
    std::atomic<bool> sawClosed{false};
    (void)engine.submitRead(*handle, 5, 64, [&](std::expected<Buffer, IoError> result) {
        sawClosed.store(!result && result.error() == IoError::closed);
        eof.store(true);
    });

    for (int spin = 0; spin < 400 && !eof.load(); ++spin) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(eof.load(), "read at end of file completed");
    check(sawClosed.load(), "end of file reports closed");

    ::unlink(path);
}

void caseStopEndsCallbacks(IoEngine &engine)
{
    auto runner = makeRunner(engine);
    runner.stop();
    runner.stop();

    std::atomic<int> after{0};
    engine.post([&] { after.fetch_add(1); });
    check(after.load() <= 1, "post after stop is not double counted");
}

} // namespace

int main()
{
    auto engine = makeEngine();
    if (!engine) {
        std::fprintf(stderr, "factory failed\n");
        return 1;
    }

    std::printf("backend: %s\n", std::string((*engine)->name()).c_str());

    casePostRunsOnEngineThread(**engine);
    std::printf("case post ok\n");
    std::fflush(stdout);
    caseSocketReadWrite(**engine);
    std::printf("case socket ok\n");
    std::fflush(stdout);
    caseFileReadIsImmediate(**engine);
    std::printf("case file ok\n");
    std::fflush(stdout);
    caseStopEndsCallbacks(**engine);
    std::printf("case stop ok\n");
    std::fflush(stdout);

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
