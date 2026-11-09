#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "domain/json/value.hpp"
#include "domain/k8s/api_error.hpp"

namespace antares::domain::k8s {

/// The `metadata` block every object carries, reduced to what a list view
/// needs. `resourceVersion` is the token a watch resumes from.
struct ObjectMeta {
    std::string name;
    std::string namespaceName;
    std::string resourceVersion;
    std::string uid;
    std::string creationTimestamp;
    std::int64_t generation = 0;

    [[nodiscard]] static ObjectMeta fromJson(const json::Value &object);
};

/// The server's failure document (FR-005). `state` is `Success` or `Failure`,
/// and `reasonToken` is the machine-readable value the mapper reads.
struct Status {
    std::string state;
    std::string reasonToken;
    std::string message;
    std::uint16_t code = 0;

    [[nodiscard]] bool isFailure() const noexcept { return state != "Success"; }

    /// The typed projection of this document.
    [[nodiscard]] ApiError toError() const;
};

/// Map a non-2xx response to the typed failure (FR-005).
///
/// A body that refuses to parse, or that is not a Status, becomes
/// `Reason::unexpected` carrying the HTTP code — never a transport error, so
/// the caller can tell "the server said no" from "the server never answered".
[[nodiscard]] ApiError mapFailure(std::uint16_t status, std::string_view body);

/// Keep a server message inside the ceiling, cutting on a character boundary
/// and marking the cut. A hostile body cannot grow a log line without bound.
[[nodiscard]] std::string bounded(std::string_view text);

} // namespace antares::domain::k8s
