#include "domain/k8s/kubeconfig.hpp"

#include <expected>
#include <utility>

#include "domain/yaml/node.hpp"

namespace antares::domain::k8s {

namespace {

/// Decode standard base64. A kubeconfig carries its PEM anchors that way, and
/// the client must hand TLS a PEM document, not the encoded text.
[[nodiscard]] std::string decodeBase64(std::string_view text) noexcept
{
    const auto valueOf = [](char digit) -> int {
        if (digit >= 'A' && digit <= 'Z') {
            return digit - 'A';
        }
        if (digit >= 'a' && digit <= 'z') {
            return 26 + (digit - 'a');
        }
        if (digit >= '0' && digit <= '9') {
            return 52 + (digit - '0');
        }
        if (digit == '+' || digit == '-') {
            return 62;
        }
        if (digit == '/' || digit == '_') {
            return 63;
        }
        return -1;
    };

    std::string out;
    out.reserve(text.size() * 3 / 4);

    std::uint32_t accumulator = 0;
    int bits = 0;
    for (const char digit : text) {
        if (digit == '=' || digit == '\n' || digit == '\r' || digit == ' ') {
            continue;
        }
        const int value = valueOf(digit);
        if (value < 0) {
            return {};
        }
        accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((accumulator >> static_cast<unsigned>(bits)) & 0xFFU);
        }
    }
    return out;
}

/// A field is either inline or in a file; the embedded form wins because it
/// survives a moved kubeconfig.
[[nodiscard]] std::string pemOf(const yaml::Node &source, std::string_view dataKey,
                                std::string_view fileKey, std::string &fileOut)
{
    const std::string_view data = source.textAt(dataKey);
    fileOut = std::string{source.textAt(fileKey)};
    if (data.empty()) {
        return {};
    }
    return decodeBase64(data);
}

} // namespace

std::string_view describe(ConfigError error) noexcept
{
    switch (error) {
    case ConfigError::missingFile:
        return "no kubeconfig found";
    case ConfigError::malformed:
        return "kubeconfig is malformed";
    case ConfigError::noCurrentContext:
        return "no current context is selected";
    case ConfigError::unknownContext:
        return "the context does not exist";
    case ConfigError::unknownCluster:
        return "the context names a cluster that does not exist";
    case ConfigError::unknownUser:
        return "the context names a user that does not exist";
    case ConfigError::noServer:
        return "the cluster has no server";
    }
    return "kubeconfig is malformed";
}

std::expected<Kubeconfig, ConfigError> Kubeconfig::parse(std::string_view text)
{
    auto document = yaml::Node::parse(text);
    if (!document || !document->isMapping()) {
        return std::unexpected(ConfigError::malformed);
    }

    Kubeconfig config;
    config.currentContext = std::string{document->textAt("current-context")};

    for (const yaml::Node &item : document->at("clusters").items()) {
        const yaml::Node &body = item.at("cluster");
        Cluster cluster;
        cluster.name = std::string{item.textAt("name")};
        cluster.server = std::string{body.textAt("server")};
        cluster.certificateAuthorityPem = pemOf(body, "certificate-authority-data",
                                               "certificate-authority",
                                               cluster.certificateAuthorityFile);
        cluster.insecureSkipTlsVerify = body.boolAt("insecure-skip-tls-verify", false);
        cluster.tlsServerName = std::string{body.textAt("tls-server-name")};
        if (cluster.name.empty()) {
            return std::unexpected(ConfigError::malformed);
        }
        config.clusters.push_back(std::move(cluster));
    }

    for (const yaml::Node &item : document->at("contexts").items()) {
        const yaml::Node &body = item.at("context");
        Context context;
        context.name = std::string{item.textAt("name")};
        context.cluster = std::string{body.textAt("cluster")};
        context.user = std::string{body.textAt("user")};
        context.defaultNamespace = std::string{body.textAt("namespace")};
        if (context.name.empty()) {
            return std::unexpected(ConfigError::malformed);
        }
        config.contexts.push_back(std::move(context));
    }

    for (const yaml::Node &item : document->at("users").items()) {
        const yaml::Node &body = item.at("user");
        User user;
        user.name = std::string{item.textAt("name")};
        user.token = std::string{body.textAt("token")};
        user.tokenFile = std::string{body.textAt("tokenFile")};
        user.username = std::string{body.textAt("username")};
        user.password = std::string{body.textAt("password")};
        user.clientCertificatePem = pemOf(body, "client-certificate-data",
                                          "client-certificate", user.clientCertificateFile);
        user.clientKeyPem =
            pemOf(body, "client-key-data", "client-key", user.clientKeyFile);
        user.execCommand = std::string{body.at("exec").textAt("command")};
        for (const yaml::Node &argument : body.at("exec").at("args").items()) {
            user.execArgs.emplace_back(argument.text());
        }
        if (user.name.empty()) {
            return std::unexpected(ConfigError::malformed);
        }
        config.users.push_back(std::move(user));
    }

    return config;
}

std::expected<Context, ConfigError> Kubeconfig::select(std::string_view overrideName) const
{
    const std::string_view wanted = overrideName.empty() ? currentContext : overrideName;
    if (wanted.empty()) {
        return std::unexpected(ConfigError::noCurrentContext);
    }
    for (const Context &context : contexts) {
        if (context.name == wanted) {
            return context;
        }
    }
    return std::unexpected(ConfigError::unknownContext);
}

const Cluster *Kubeconfig::findCluster(std::string_view name) const noexcept
{
    for (const Cluster &cluster : clusters) {
        if (cluster.name == name) {
            return &cluster;
        }
    }
    return nullptr;
}

const User *Kubeconfig::findUser(std::string_view name) const noexcept
{
    for (const User &user : users) {
        if (user.name == name) {
            return &user;
        }
    }
    return nullptr;
}

} // namespace antares::domain::k8s
