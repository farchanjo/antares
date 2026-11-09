#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include "domain/http/http_error.hpp"

namespace antares::domain::http {

/// How a request proves identity. Closed: a new mechanism is a deliberate act.
enum class AuthKind : std::uint8_t { none, bearer, basic, clientCertificate };

/// Credential material. Never logged; `describe` returns the kind only.
struct Auth {
    AuthKind kind = AuthKind::none;

    /// Bearer token or the basic user name.
    std::string principal;

    /// Basic password. Empty for the other kinds.
    std::string secret;

    /// PEM-encoded client certificate and key, for mutual TLS.
    std::string certificatePem;
    std::string privateKeyPem;

    [[nodiscard]] bool isSet() const noexcept { return kind != AuthKind::none; }

    [[nodiscard]] static Auth bearer(std::string token);
    [[nodiscard]] static Auth basic(std::string user, std::string password);
    [[nodiscard]] static Auth clientCertificate(std::string certificate, std::string key);

    /// The `Authorization` field value (RFC 9110 §11.6). Empty for mutual TLS,
    /// which proves identity in the handshake instead.
    [[nodiscard]] std::expected<std::string, HttpError> authorizationValue() const;
};

} // namespace antares::domain::http
