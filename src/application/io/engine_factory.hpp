#pragma once

#include <expected>
#include <string>

#include <memory>

#include "ports/io/io_engine.hpp"

namespace antares::application::io {

/// Build the engine for this platform (ADR 0006, ADR 0009).
///
/// kqueue is the engine. The fallback exists for the case the kernel refuses
/// the queue, and for an explicit override during diagnosis. Callers receive
/// the port and never learn which one answered.
[[nodiscard]] std::expected<std::unique_ptr<ports::io::IoEngine>, ports::io::IoError>
makeEngine();

} // namespace antares::application::io
