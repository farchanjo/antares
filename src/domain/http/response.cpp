#include "domain/http/response.hpp"

#include <algorithm>
#include <cctype>

namespace antares::domain::http {
namespace {

bool sameIgnoringCase(std::string_view left, std::string_view right) noexcept
{
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           });
}

} // namespace

const std::string *Response::find(std::string_view name) const noexcept
{
    for (const Header &header : headers) {
        if (sameIgnoringCase(header.name, name)) {
            return &header.value;
        }
    }
    return nullptr;
}

} // namespace antares::domain::http
