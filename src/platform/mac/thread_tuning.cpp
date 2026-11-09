#include "platform/mac/thread_tuning.hpp"

#include <pthread.h>

#include <string>

namespace antares::platform::mac {
namespace {

constexpr std::size_t MAX_NAME = 63;

} // namespace

void tuneLoopThread(std::string_view name) noexcept
{
    std::string truncated{name.substr(0, MAX_NAME)};
    (void)::pthread_setname_np(truncated.c_str());

    // USER_INTERACTIVE keeps the loop ahead of batch work without claiming the
    // audio/realtime class, which needs a bounded worst case we do not have yet.
    (void)::pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
}

} // namespace antares::platform::mac
