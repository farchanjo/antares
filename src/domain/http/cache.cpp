#include "domain/http/cache.hpp"

#include <algorithm>
#include <cctype>

namespace antares::domain::http {
namespace {

[[nodiscard]] bool isField(std::string_view name, std::string_view wanted) noexcept
{
    return name.size() == wanted.size() &&
           std::equal(name.begin(), name.end(), wanted.begin(), [](char left, char right) {
               return std::tolower(static_cast<unsigned char>(left)) == right;
           });
}

[[nodiscard]] std::string fieldOf(const Response &response, std::string_view wanted)
{
    for (const Header &header : response.headers) {
        if (isField(header.name, wanted)) {
            return header.value;
        }
    }
    return {};
}

} // namespace

void ConditionalCache::store(std::string_view url, const Response &response)
{
    // Only a cacheable status carries a representation worth keeping.
    if (response.status != 200) {
        return;
    }

    Entry entry;
    entry.validators.etag = fieldOf(response, "etag");
    entry.validators.lastModified = fieldOf(response, "last-modified");
    if (entry.validators.empty()) {
        entries_.erase(std::string{url});
        return;
    }

    entry.response = response;
    entries_.insert_or_assign(std::string{url}, std::move(entry));
}

std::optional<Validators> ConditionalCache::validatorsFor(std::string_view url) const
{
    const auto found = entries_.find(std::string{url});
    if (found == entries_.end() || found->second.validators.empty()) {
        return std::nullopt;
    }
    return found->second.validators;
}

std::optional<Response> ConditionalCache::take(std::string_view url)
{
    const auto found = entries_.find(std::string{url});
    if (found == entries_.end()) {
        return std::nullopt;
    }
    Response kept = found->second.response;
    entries_.erase(found);
    return kept;
}

} // namespace antares::domain::http
