#pragma once

#include <cstddef>
#include <string_view>

namespace antares::platform::linux {

void tuneLoopThread(std::string_view name) noexcept;

[[nodiscard]] constexpr std::size_t loopStackBytes() noexcept { return 512 * 1024; }

} // namespace antares::platform::linux
