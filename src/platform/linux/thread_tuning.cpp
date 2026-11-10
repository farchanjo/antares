#include "platform/linux/thread_tuning.hpp"

#include <pthread.h>

#include <string>

namespace antares::platform::linux {
namespace {

constexpr std::size_t MAX_NAME = 15;

} // namespace

void tuneLoopThread(std::string_view name) noexcept
{
    std::string const truncated{name.substr(0, MAX_NAME)};
    (void)::pthread_setname_np(::pthread_self(), truncated.c_str());
}

} // namespace antares::platform::linux
