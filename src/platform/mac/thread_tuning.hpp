#pragma once

#include <cstddef>
#include <string_view>

namespace antares::platform::mac {

/// Name the calling thread so a sampler shows it, and give it the latency
/// class: the loop is a consumer, not background batch work.
void tuneLoopThread(std::string_view name) noexcept;

/// Stack the loop actually needs; the default 8 MiB is waste per loop.
[[nodiscard]] constexpr std::size_t loopStackBytes() noexcept { return 512 * 1024; }

} // namespace antares::platform::mac
