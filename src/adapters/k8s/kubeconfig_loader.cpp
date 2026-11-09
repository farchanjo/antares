#include "adapters/k8s/kubeconfig_loader.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace antares::adapters::k8s {

namespace {

[[nodiscard]] std::string homeDirectory()
{
    const char *home = std::getenv("HOME");
    return home == nullptr ? std::string{} : std::string{home};
}

[[nodiscard]] std::string fromEnvironment(std::string_view name)
{
    const std::string key{name};
    const char *value = std::getenv(key.c_str());
    return value == nullptr ? std::string{} : std::string{value};
}

[[nodiscard]] std::expected<std::string, domain::k8s::ConfigError>
readFile(const std::string &path)
{
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::unexpected(domain::k8s::ConfigError::missingFile);
    }
    std::ostringstream sink;
    sink << file.rdbuf();
    return sink.str();
}

/// The first existing candidate, or empty when none does.
[[nodiscard]] std::string resolvePath(std::string_view explicitPath)
{
    if (!explicitPath.empty()) {
        return std::string{explicitPath};
    }
    const std::string fromEnv = fromEnvironment("KUBECONFIG");
    if (!fromEnv.empty()) {
        // A list is colon-separated on POSIX; the first entry wins here, because
        // merging several documents needs its own rule set.
        const std::size_t colon = fromEnv.find(':');
        return colon == std::string::npos ? fromEnv : fromEnv.substr(0, colon);
    }
    const std::string home = homeDirectory();
    if (home.empty()) {
        return {};
    }
    return home + "/.kube/config";
}

} // namespace

std::expected<domain::k8s::Kubeconfig, domain::k8s::ConfigError>
KubeconfigLoader::load(std::string_view explicitPath)
{
    const std::string path = resolvePath(explicitPath);
    if (path.empty()) {
        return std::unexpected(domain::k8s::ConfigError::missingFile);
    }

    auto text = readFile(path);
    if (!text) {
        return std::unexpected(text.error());
    }
    return domain::k8s::Kubeconfig::parse(*text);
}

bool KubeconfigLoader::present()
{
    const std::string path = resolvePath({});
    if (path.empty()) {
        return false;
    }
    std::ifstream file{path, std::ios::binary};
    return static_cast<bool>(file);
}

std::expected<domain::k8s::User, domain::k8s::ConfigError>
FileCredentialSource::resolve(const domain::k8s::User &user)
{
    domain::k8s::User resolved = user;

    // A token file wins over an inline token only when the inline value is
    // absent: an inline token is what the operator last wrote.
    if (resolved.token.empty() && !resolved.tokenFile.empty()) {
        auto text = readFile(resolved.tokenFile);
        if (!text) {
            return std::unexpected(domain::k8s::ConfigError::malformed);
        }
        // A token file ends with a newline, which must not reach the header.
        while (!text->empty() && (text->back() == '\n' || text->back() == '\r')) {
            text->pop_back();
        }
        resolved.token = std::move(*text);
    }

    if (resolved.clientCertificatePem.empty() && !resolved.clientCertificateFile.empty()) {
        auto text = readFile(resolved.clientCertificateFile);
        if (!text) {
            return std::unexpected(domain::k8s::ConfigError::malformed);
        }
        resolved.clientCertificatePem = std::move(*text);
    }

    if (resolved.clientKeyPem.empty() && !resolved.clientKeyFile.empty()) {
        auto text = readFile(resolved.clientKeyFile);
        if (!text) {
            return std::unexpected(domain::k8s::ConfigError::malformed);
        }
        resolved.clientKeyPem = std::move(*text);
    }

    if (resolved.empty()) {
        // An exec plugin or an auth provider is a command to run, and running
        // one without being asked would be a surprise. Report it as unresolved.
        return std::unexpected(domain::k8s::ConfigError::unknownUser);
    }
    return resolved;
}

} // namespace antares::adapters::k8s
