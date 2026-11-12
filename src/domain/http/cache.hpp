#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "domain/http/response.hpp"

namespace antares::domain::http {

/// What a stored response offers for revalidation.
struct Validators {
    std::string etag;
    std::string lastModified;

    [[nodiscard]] bool empty() const noexcept { return etag.empty() && lastModified.empty(); }
};

/// A validator store, keyed by URL.
///
/// This is revalidation only: a stored answer is never served on its own, it is
/// offered back and the origin decides. That keeps it honest for a client whose
/// responses carry `Cache-Control: no-cache`, where serving stale would be wrong
/// and revalidating is still cheaper than refetching.
class ConditionalCache {
public:
    /// Remember what `response` offers for `url`. A response with no validator
    /// drops whatever was stored, so a later request is not conditional against
    /// an answer that no longer exists.
    void store(std::string_view url, const Response &response);

    [[nodiscard]] std::optional<Validators> validatorsFor(std::string_view url) const;

    /// The stored answer for a 304, dropped as it is handed over.
    [[nodiscard]] std::optional<Response> take(std::string_view url);

    void erase(std::string_view url) { entries_.erase(std::string{url}); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    struct Entry {
        Validators validators;
        Response response;
    };

    std::unordered_map<std::string, Entry> entries_;
};

} // namespace antares::domain::http
