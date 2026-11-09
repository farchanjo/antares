#include "domain/http/auth.hpp"

#include <utility>

namespace antares::domain::http {

Auth Auth::bearer(std::string token)
{
    Auth auth;
    auth.kind = AuthKind::bearer;
    auth.principal = std::move(token);
    return auth;
}

Auth Auth::basic(std::string user, std::string password)
{
    Auth auth;
    auth.kind = AuthKind::basic;
    auth.principal = std::move(user);
    auth.secret = std::move(password);
    return auth;
}

Auth Auth::clientCertificate(std::string certificate, std::string key)
{
    Auth auth;
    auth.kind = AuthKind::clientCertificate;
    auth.certificatePem = std::move(certificate);
    auth.privateKeyPem = std::move(key);
    return auth;
}

} // namespace antares::domain::http
