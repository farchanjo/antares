#include "domain/http/retry_policy.hpp"

namespace antares::domain::http {

bool RetryPolicy::isRetryableStatus(std::uint16_t status) const noexcept
{
    for (const std::uint16_t candidate : retryOnStatus) {
        if (candidate == status) {
            return true;
        }
    }
    return false;
}

} // namespace antares::domain::http
