#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "domain/k8s/api_error.hpp"

namespace antares::domain::k8s {

/// Why a kubeconfig could not be used. Each case is distinct on purpose: the UI
/// has to tell "no file" from "the context names a cluster that is not there".
enum class ConfigError : std::uint8_t {
    missingFile,
    malformed,
    noCurrentContext,
    unknownContext,
    unknownCluster,
    unknownUser,
    noServer,
};

[[nodiscard]] std::string_view describe(ConfigError error) noexcept;

/// One cluster entry.
struct Cluster {
    std::string name;
    std::string server;
    /// PEM anchors, already base64-decoded from `certificate-authority-data`.
    std::string certificateAuthorityPem;
    /// Path to a PEM bundle, used when no embedded data is present.
    std::string certificateAuthorityFile;
    bool insecureSkipTlsVerify = false;
    /// Overrides the server's port; empty means the URL decides.
    std::string tlsServerName;
};

/// One user entry. The fields are the raw kubeconfig values; resolving them
/// into a usable credential is a separate step so the parse stays pure.
struct User {
    std::string name;
    std::string token;
    std::string tokenFile;
    std::string username;
    std::string password;
    std::string clientCertificatePem;
    std::string clientKeyPem;
    std::string clientCertificateFile;
    std::string clientKeyFile;
    std::string execCommand;
    std::vector<std::string> execArgs;

    [[nodiscard]] bool empty() const noexcept
    {
        return token.empty() && tokenFile.empty() && username.empty() &&
               clientCertificatePem.empty() && clientCertificateFile.empty() &&
               execCommand.empty();
    }
};

/// One context entry, tying a cluster to a user.
struct Context {
    std::string name;
    std::string cluster;
    std::string user;
    std::string defaultNamespace;
};

/// A parsed kubeconfig document (FR-006).
struct Kubeconfig {
    std::vector<Cluster> clusters;
    std::vector<User> users;
    std::vector<Context> contexts;
    std::string currentContext;

    /// Parse a document. A subset of YAML is accepted; anything outside it is
    /// refused as `malformed` rather than half-read.
    [[nodiscard]] static std::expected<Kubeconfig, ConfigError> parse(std::string_view text);

    /// The selected context: `overrideName` when set, else the current one.
    [[nodiscard]] std::expected<Context, ConfigError>
    select(std::string_view overrideName) const;

    [[nodiscard]] const Cluster *findCluster(std::string_view name) const noexcept;
    [[nodiscard]] const User *findUser(std::string_view name) const noexcept;
};

} // namespace antares::domain::k8s
