#include "adapters/ui/page_error.hpp"

namespace antares::adapters::ui {

std::string_view pageErrorName(PageError error) noexcept
{
    switch (error) {
    case PageError::transport:
        return "transport";
    case PageError::refused:
        return "refused";
    case PageError::unknownScope:
        return "unknown-scope";
    }
    return "transport";
}

} // namespace antares::adapters::ui
