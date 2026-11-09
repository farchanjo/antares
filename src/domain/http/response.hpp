#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "domain/http/request.hpp"

namespace antares::domain::http {

/// What the caller receives. `hops` counts redirects already followed.
struct Response {
    std::uint16_t status = 0;
    Version version = Version::negotiate;
    std::vector<Header> headers;
    Body body;
    std::uint8_t hops = 0;

    [[nodiscard]] bool isRedirect() const noexcept
    {
        return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    }

    [[nodiscard]] const std::string *find(std::string_view name) const noexcept;
};

} // namespace antares::domain::http
