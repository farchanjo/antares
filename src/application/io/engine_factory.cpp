#include "application/io/engine_factory.hpp"

#include <cstdlib>
#include <cstring>
#include <expected>
#include <string>
#include <string_view>

#if defined(__APPLE__)
#include "adapters/platform/io/kqueue_engine.hpp"
#elif defined(__linux__)
#include "adapters/platform/io/epoll_engine.hpp"
#include "adapters/platform/io/io_uring_engine.hpp"
#endif
#include "adapters/platform/io/poll_engine.hpp"

namespace antares::application::io {
namespace {

/// The backend the environment pins, or null. `ANTARES_IO_BACKEND` is the
/// escape hatch, so a refused kernel engine never blocks a run.
[[nodiscard]] const char *forcedBackend() noexcept
{
    const char *value = std::getenv("ANTARES_IO_BACKEND");
    // An empty value is "not forced", which is what `VAR=` in a test environment
    // means; without this the name would be the empty string.
    return value != nullptr && value[0] != '\0' ? value : nullptr;
}

[[nodiscard]] bool forcedIs(const char *name) noexcept
{
    const char *requested = forcedBackend();
    return requested != nullptr && std::strcmp(requested, name) == 0;
}

bool overrideRequestsPoll() noexcept
{
    return forcedIs("poll");
}

#if defined(__APPLE__) || defined(__linux__)
constexpr bool KERNEL_ENGINE_COMPILED_IN = true;
constexpr std::string_view KERNEL_ENGINE_NAME = 
#if defined(__APPLE__)
    "kqueue";
#else
    "epoll";
#endif
#else
constexpr bool KERNEL_ENGINE_COMPILED_IN = false;
constexpr std::string_view KERNEL_ENGINE_NAME = "poll";
#endif

} // namespace

std::expected<std::unique_ptr<ports::io::IoEngine>, ports::io::IoError> makeEngine()
{
#if defined(__APPLE__)
    if (!overrideRequestsPoll()) {
        auto engine = std::make_unique<adapters::platform::io::KqueueEngine>();
        if (engine->ready()) {
            return std::unique_ptr<ports::io::IoEngine>{std::move(engine)};
        }
    }
#elif defined(__linux__)
    // io_uring first, epoll as the fallback (ADR 0018). A kernel with the ring
    // disabled or a seccomp profile that blocks it lands on epoll, not on a
    // failed run.
    if (!forcedIs("epoll") && !overrideRequestsPoll()) {
        auto engine = std::make_unique<adapters::platform::io::IoUringEngine>();
        if (engine->ready()) {
            return std::unique_ptr<ports::io::IoEngine>{std::move(engine)};
        }
    }
    if (!overrideRequestsPoll()) {
        auto engine = std::make_unique<adapters::platform::io::EpollEngine>();
        if (engine->ready()) {
            return std::unique_ptr<ports::io::IoEngine>{std::move(engine)};
        }
    }
#endif

    return std::unique_ptr<ports::io::IoEngine>{
        std::make_unique<adapters::platform::io::PollEngine>()};
}

} // namespace antares::application::io
