#include "domain/http/retry_policy.hpp"

namespace antares::domain::http {

bool RetryPolicy::isRetryableStatus(std::uint16_t status) const noexcept
{
    return std::ranges::any_of(retryOnStatus,
                               [status](std::uint16_t candidate) { return candidate == status; });
}

} // namespace antares::domain::http
