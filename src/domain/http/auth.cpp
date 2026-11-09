#include "domain/http/auth.hpp"

#include <array>
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

namespace {

constexpr char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(std::string_view text)
{
    std::string out;
    out.reserve(((text.size() + 2) / 3) * 4);

    std::size_t index = 0;
    while (index + 3 <= text.size()) {
        const std::uint32_t block = (static_cast<std::uint8_t>(text[index]) << 16) |
                                    (static_cast<std::uint8_t>(text[index + 1]) << 8) |
                                    static_cast<std::uint8_t>(text[index + 2]);
        out += ALPHABET[(block >> 18) & 0x3F];
        out += ALPHABET[(block >> 12) & 0x3F];
        out += ALPHABET[(block >> 6) & 0x3F];
        out += ALPHABET[block & 0x3F];
        index += 3;
    }

    const std::size_t remaining = text.size() - index;
    if (remaining == 1) {
        const std::uint32_t block = static_cast<std::uint8_t>(text[index]) << 16;
        out += ALPHABET[(block >> 18) & 0x3F];
        out += ALPHABET[(block >> 12) & 0x3F];
        out += "==";
    } else if (remaining == 2) {
        const std::uint32_t block = (static_cast<std::uint8_t>(text[index]) << 16) |
                                    (static_cast<std::uint8_t>(text[index + 1]) << 8);
        out += ALPHABET[(block >> 18) & 0x3F];
        out += ALPHABET[(block >> 12) & 0x3F];
        out += ALPHABET[(block >> 6) & 0x3F];
        out += '=';
    }
    return out;
}

} // namespace

std::expected<std::string, HttpError> Auth::authorizationValue() const
{
    switch (kind) {
    case AuthKind::none:
        return std::string{};
    case AuthKind::bearer:
        return "Bearer " + principal;
    case AuthKind::basic:
        return "Basic " + base64(principal + ":" + secret);
    case AuthKind::clientCertificate:
        return std::string{};
    }
    return std::unexpected(HttpError::protocol);
}

} // namespace antares::domain::http
