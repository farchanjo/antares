#include "application/http/blocking_client.hpp"

#include <expected>
#include <utility>

namespace antares::application::http {

std::expected<domain::http::Response, domain::http::HttpError>
BlockingClient::send(domain::http::Request request)
{
    using Result = std::expected<domain::http::Response, domain::http::HttpError>;

    if (pool_.isLoopThread()) {
        return std::unexpected(domain::http::HttpError::reentrant);
    }

    std::mutex mutex;
    std::condition_variable settled;
    bool ready = false;
    std::optional<Result> outcome;

    auto submitted = client_.submit(
        std::move(request), [&](Result result) {
            {
                const std::lock_guard<std::mutex> guard{mutex};
                outcome.emplace(std::move(result));
                ready = true;
            }
            settled.notify_one();
        });

    if (!submitted) {
        return std::unexpected(submitted.error());
    }

    std::unique_lock<std::mutex> lock{mutex};
    settled.wait(lock, [&ready] { return ready; });
    if (!outcome.has_value()) {
        return std::unexpected(domain::http::HttpError::closed);
    }
    return std::move(*outcome);
}

} // namespace antares::application::http
