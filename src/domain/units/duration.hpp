#pragma once

#include <chrono>

namespace antares::domain {

/// Native duration, aliased so a signature reads as a span of time. The
/// standard library already carries the literals, the arithmetic and the
/// comparison; nothing here re-implements them.
using Duration = std::chrono::milliseconds;

namespace literals {

using namespace std::chrono_literals;

} // namespace literals

} // namespace antares::domain
