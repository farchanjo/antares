#include "domain/k8s/store_error.hpp"

namespace antares::domain::k8s {

std::string_view storeErrorName(StoreError error) noexcept
{
    switch (error) {
    case StoreError::invalidUid:
        return "invalid uid";
    case StoreError::invalidName:
        return "invalid name";
    case StoreError::invalidNamespace:
        return "invalid namespace";
    case StoreError::invalidCluster:
        return "invalid cluster";
    case StoreError::invalidKind:
        return "invalid kind";
    case StoreError::invalidVersion:
        return "invalid resource version";
    case StoreError::tooManyScopes:
        return "too many scopes";
    case StoreError::unknownScope:
        return "unknown scope";
    case StoreError::unknownEntry:
        return "unknown entry";
    case StoreError::notRelisting:
        return "not relisting";
    }
    return "invalid";
}

} // namespace antares::domain::k8s
