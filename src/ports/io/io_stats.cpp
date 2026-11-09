#include "ports/io/io_stats.hpp"

namespace antares::ports::io {

static_assert(static_cast<std::uint8_t>(IoError::system) < FAILURE_FAMILIES,
              "every IoError case needs a failure bucket");

} // namespace antares::ports::io
