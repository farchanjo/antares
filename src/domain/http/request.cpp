#include "domain/http/request.hpp"

namespace antares::domain::http {

void Request::setBody(std::string_view text)
{
    body.assign(reinterpret_cast<const std::byte *>(text.data()),
                reinterpret_cast<const std::byte *>(text.data()) + text.size());
}

} // namespace antares::domain::http
