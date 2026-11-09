#include "domain/k8s/status.hpp"

#include <algorithm>

namespace antares::domain::k8s {

namespace {

/// Read a string member, defaulting to empty. Every field of a Status is
/// optional on the wire, so an absent one is not an error.
[[nodiscard]] std::string textOf(const json::Value &document, std::string_view key)
{
    return std::string{document.textAt(key)};
}

/// The reason implied by the HTTP code alone, for a body that carries no token.
[[nodiscard]] Reason reasonFromHttp(std::uint16_t status) noexcept
{
    switch (status) {
    case 401:
        return Reason::unauthorized;
    case 403:
        return Reason::forbidden;
    case 404:
        return Reason::notFound;
    case 409:
        return Reason::conflict;
    case 410:
        return Reason::expired;
    case 429:
        return Reason::tooManyRequests;
    default:
        break;
    }
    return status >= 500 ? Reason::serviceUnavailable : Reason::unexpected;
}

} // namespace

std::string bounded(std::string_view text)
{
    if (text.size() <= ApiError::messageCeiling) {
        return std::string{text};
    }

    // Cut below the ceiling and walk back off a continuation byte, so the
    // result is still valid UTF-8.
    std::size_t cut = ApiError::messageCeiling - 3;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) {
        --cut;
    }
    std::string out{text.substr(0, cut)};
    out += "...";
    return out;
}

ObjectMeta ObjectMeta::fromJson(const json::Value &object)
{
    const json::Value &metadata = object.member("metadata");
    ObjectMeta meta;
    meta.name = textOf(metadata, "name");
    meta.namespaceName = textOf(metadata, "namespace");
    meta.resourceVersion = textOf(metadata, "resourceVersion");
    meta.uid = textOf(metadata, "uid");
    meta.creationTimestamp = textOf(metadata, "creationTimestamp");
    meta.generation = metadata.intAt("generation").value_or(0);
    return meta;
}

ApiError Status::toError() const
{
    ApiError error;
    error.reason = reasonFromToken(reasonToken);
    error.code = code;
    error.message = bounded(message);
    return error;
}

ApiError mapFailure(std::uint16_t status, std::string_view body)
{
    ApiError error;
    error.code = status;

    const auto document = json::Value::parse(body);
    if (!document || !document->isObject()) {
        error.reason = reasonFromHttp(status);
        error.message = bounded(body);
        return error;
    }

    Status parsed;
    parsed.state = textOf(*document, "status");
    parsed.reasonToken = textOf(*document, "reason");
    parsed.message = textOf(*document, "message");
    parsed.code = status;

    if (parsed.reasonToken.empty()) {
        // A Status with no token: trust the code rather than guess a reason.
        error.reason = reasonFromHttp(status);
        error.message = bounded(parsed.message.empty() ? body : parsed.message);
        return error;
    }
    return parsed.toError();
}

} // namespace antares::domain::k8s
