#include "ports/io/io_engine.hpp"

namespace antares::ports::io {

static_assert(sizeof(IoError) == 1, "IoError stays a single byte");

} // namespace antares::ports::io
