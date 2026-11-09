#include "application/io/engine_factory.hpp"

#include <cstdlib>
#include <cstring>

#if defined(__APPLE__)
#include "adapters/platform/io/kqueue_engine.hpp"
#endif
#include "adapters/platform/io/poll_engine.hpp"

namespace antares::application::io {
namespace {

bool overrideRequestsPoll() noexcept
{
    const char *requested = std::getenv("ANTARES_IO_BACKEND");
    return requested != nullptr && std::strcmp(requested, "poll") == 0;
}

bool kqueueCompiledIn() noexcept
{
#if defined(__APPLE__)
    return true;
#else
    return false;
#endif
}

} // namespace

const char *selectedBackendName() noexcept
{
    if (overrideRequestsPoll()) {
        return "poll";
    }
    return kqueueCompiledIn() ? "kqueue" : "poll";
}

std::expected<std::unique_ptr<ports::io::IoEngine>, ports::io::IoError> makeEngine()
{
#if defined(__APPLE__)
    if (!overrideRequestsPoll()) {
        auto engine = std::make_unique<adapters::platform::io::KqueueEngine>();
        if (engine->ready()) {
            return std::unique_ptr<ports::io::IoEngine>{std::move(engine)};
        }
    }
#endif

    return std::unique_ptr<ports::io::IoEngine>{
        std::make_unique<adapters::platform::io::PollEngine>()};
}

} // namespace antares::application::io
