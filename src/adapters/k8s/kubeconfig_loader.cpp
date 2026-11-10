#include "adapters/k8s/kubeconfig_loader.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

namespace antares::adapters::k8s {

namespace {

constexpr std::string_view pathSeparator = ":";

[[nodiscard]] std::string fromEnvironment(std::string_view name)
{
    const std::string key{name};
    const char *value = std::getenv(key.c_str());
    return value == nullptr ? std::string{} : std::string{value};
}

[[nodiscard]] std::string homeDirectory()
{
    return fromEnvironment("HOME");
}

/// A file's contents, or an empty string. Absence is not fatal at this layer:
/// the caller decides whether a missing anchor is a problem.
[[nodiscard]] std::string readFile(const std::string &path)
{
    if (path.empty()) {
        return {};
    }
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return {};
    }
    std::ostringstream sink;
    sink << file.rdbuf();
    return sink.str();
}

/// A token file ends with a newline, and a newline inside a header is a
/// malformed request.
[[nodiscard]] std::string withoutTrailingSpace(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

void splitInto(std::string_view list, std::vector<std::string> &out)
{
    std::size_t start = 0;
    while (start <= list.size()) {
        const std::size_t at = list.find(pathSeparator, start);
        const std::size_t stop = at == std::string_view::npos ? list.size() : at;
        if (stop > start) {
            out.emplace_back(list.substr(start, stop - start));
        }
        if (at == std::string_view::npos) {
            return;
        }
        start = at + pathSeparator.size();
    }
}

/// First entry wins: a name already present is kept, a new one is appended.
template <typename T>
void mergeByName(std::vector<T> &sink, std::vector<T> source,
                 const auto &nameOf)
{
    for (T &item : source) {
        bool seen = false;
        for (const T &existing : sink) {
            if (nameOf(existing) == nameOf(item)) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            sink.push_back(std::move(item));
        }
    }
}

} // namespace

std::vector<std::string> KubeconfigLoader::searchPaths()
{
    std::vector<std::string> paths;
    splitInto(fromEnvironment("KUBECONFIG"), paths);
    if (!paths.empty()) {
        return paths;
    }
    const std::string home = homeDirectory();
    if (!home.empty()) {
        paths.push_back(home + "/.kube/config");
    }
    return paths;
}

domain::k8s::Kubeconfig merge(const std::vector<domain::k8s::Kubeconfig> &documents)
{
    domain::k8s::Kubeconfig merged;
    const auto clusterName = [](const domain::k8s::Cluster &c) { return c.name; };
    const auto userName = [](const domain::k8s::User &u) { return u.name; };
    const auto contextName = [](const domain::k8s::Context &c) { return c.name; };

    for (const domain::k8s::Kubeconfig &document : documents) {
        mergeByName(merged.clusters, document.clusters, clusterName);
        mergeByName(merged.users, document.users, userName);
        mergeByName(merged.contexts, document.contexts, contextName);
        if (merged.currentContext.empty()) {
            merged.currentContext = document.currentContext;
        }
    }
    return merged;
}

void KubeconfigLoader::hydrate(domain::k8s::Kubeconfig &config)
{
    for (domain::k8s::Cluster &cluster : config.clusters) {
        if (cluster.certificateAuthorityPem.empty() && !cluster.certificateAuthorityFile.empty()) {
            cluster.certificateAuthorityPem = readFile(cluster.certificateAuthorityFile);
        }
    }

    for (domain::k8s::User &user : config.users) {
        if (user.token.empty() && !user.tokenFile.empty()) {
            user.token = withoutTrailingSpace(readFile(user.tokenFile));
        }
        if (user.clientCertificatePem.empty() && !user.clientCertificateFile.empty()) {
            user.clientCertificatePem = readFile(user.clientCertificateFile);
        }
        if (user.clientKeyPem.empty() && !user.clientKeyFile.empty()) {
            user.clientKeyPem = readFile(user.clientKeyFile);
        }
    }
}

std::expected<domain::k8s::Kubeconfig, domain::k8s::ConfigError>
KubeconfigLoader::load(const std::vector<std::string> &paths)
{
    const std::vector<std::string> candidates = paths.empty() ? searchPaths() : paths;
    if (candidates.empty()) {
        return std::unexpected(domain::k8s::ConfigError::missingFile);
    }

    std::vector<domain::k8s::Kubeconfig> documents;
    for (const std::string &path : candidates) {
        const std::string text = readFile(path);
        if (text.empty()) {
            continue;
        }
        auto parsed = domain::k8s::Kubeconfig::parse(text);
        if (!parsed) {
            // A broken file is refused by name, not silently skipped: skipping
            // it would hide the very file the user is trying to fix.
            return std::unexpected(parsed.error());
        }
        documents.push_back(std::move(*parsed));
    }

    if (documents.empty()) {
        return std::unexpected(domain::k8s::ConfigError::missingFile);
    }

    domain::k8s::Kubeconfig merged = merge(documents);
    hydrate(merged);
    return merged;
}

bool KubeconfigLoader::present()
{
    for (const std::string &path : searchPaths()) {
        std::ifstream file{path, std::ios::binary};
        if (file) {
            return true;
        }
    }
    return false;
}

std::expected<domain::k8s::User, domain::k8s::ConfigError>
FileCredentialSource::resolve(const domain::k8s::User &user)
{
    domain::k8s::User resolved = user;

    if (resolved.token.empty() && !resolved.tokenFile.empty()) {
        resolved.token = withoutTrailingSpace(readFile(resolved.tokenFile));
    }
    if (resolved.clientCertificatePem.empty() && !resolved.clientCertificateFile.empty()) {
        resolved.clientCertificatePem = readFile(resolved.clientCertificateFile);
    }
    if (resolved.clientKeyPem.empty() && !resolved.clientKeyFile.empty()) {
        resolved.clientKeyPem = readFile(resolved.clientKeyFile);
    }

    if (!resolved.execCommand.empty() && resolved.empty()) {
        // An exec plugin is a command to run. Running one without being asked
        // would be a surprise, so it is reported as unresolved instead. A user
        // with no credential at all is anonymous, which is legal.
        return std::unexpected(domain::k8s::ConfigError::unknownUser);
    }
    return resolved;
}

} // namespace antares::adapters::k8s
