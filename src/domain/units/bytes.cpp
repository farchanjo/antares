#include "domain/units/bytes.hpp"

#include <type_traits>

namespace antares::domain {

static_assert(!std::is_convertible_v<std::size_t, Bytes>, "Bytes is explicit on purpose");
static_assert(sizeof(Bytes) == sizeof(std::size_t), "Bytes must stay a register value");

} // namespace antares::domain
