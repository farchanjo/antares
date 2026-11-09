#include "domain/k8s/api_error.hpp"

#include <array>
#include <utility>

namespace antares::domain::k8s {

namespace {

struct Pair {
    std::string_view token;
    Reason reason;
};

/// The tokens the API server uses, plus the ones a watch frame carries.
constexpr std::array<Pair, 11> table{{{"NotFound", Reason::notFound},
                                      {"AlreadyExists", Reason::alreadyExists},
                                      {"Conflict", Reason::conflict},
                                      {"Forbidden", Reason::forbidden},
                                      {"Unauthorized", Reason::unauthorized},
                                      {"Invalid", Reason::invalid},
                                      {"Timeout", Reason::timeout},
                                      {"TooManyRequests", Reason::tooManyRequests},
                                      {"InternalError", Reason::internalError},
                                      {"ServiceUnavailable", Reason::serviceUnavailable},
                                      {"Expired", Reason::expired}}};

} // namespace

Reason reasonFromToken(std::string_view token) noexcept
{
    for (const Pair &entry : table) {
        if (entry.token == token) {
            return entry.reason;
        }
    }
    return Reason::unexpected;
}

bool isRetryable(Reason reason) noexcept
{
    return reason == Reason::tooManyRequests || reason == Reason::serviceUnavailable ||
           reason == Reason::timeout || reason == Reason::conflict;
}

} // namespace antares::domain::k8s
