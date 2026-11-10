// Throwaway: how does completion cost scale with the number of in-flight ops?
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

#include "application/io/engine_factory.hpp"
#include "domain/units/bytes.hpp"
#include "ports/io/io_handle.hpp"

namespace application = antares::application;
namespace domain = antares::domain;

using antares::ports::io::Buffer;
using antares::ports::io::IoEngine;
using antares::ports::io::IoError;
using antares::ports::io::IoHandle;

int main(int argc, char **argv)
{
    const int total = argc > 1 ? std::atoi(argv[1]) : 2000;

    int pair[2] = {-1, -1};
    ::socketpair(AF_UNIX, SOCK_STREAM, 0, pair);

    auto built = application::io::makeEngine();
    IoEngine &engine = **built;
    std::thread loop{[&engine] { engine.run(); }};

    auto reader = IoHandle::adopt(pair[0]);
    auto writer = IoHandle::adopt(pair[1]);

    std::atomic<int> done{0};
    auto started = std::chrono::steady_clock::now();

    // Everything is submitted from the loop thread, so the run measures the
    // engine and not a race on its op table.
    engine.post([&engine, &reader, &done, total, writeEnd = pair[1]] {
        for (int index = 0; index < total; ++index) {
            static_cast<void>(
                engine.submitRead(*reader, std::nullopt, domain::Bytes{1},
                                  [&done](std::expected<Buffer, IoError>) { ++done; }));
            const char byte = 'x';
            (void)::write(writeEnd, &byte, 1);
        }
    });
    while (done.load() < total) {
        std::this_thread::yield();
    }

    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - started)
                            .count();
    std::printf("%7d ops  %8lld us  %6.2f us/op\n", total, static_cast<long long>(micros),
                static_cast<double>(micros) / total);

    engine.stop();
    loop.join();
    return 0;
}
