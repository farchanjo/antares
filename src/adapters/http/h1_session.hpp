#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string>

#include "domain/http/http_error.hpp"
#include "domain/http/request.hpp"
#include "domain/http/response.hpp"
#include "domain/units/bytes.hpp"

namespace antares::adapters::http {

/// Serializes a request and parses the response, HTTP/1.1 only (FR-005).
///
/// The parser is incremental: bytes arrive in whatever size the socket gives and
/// the ceiling is checked before every append (FR-010).
class H1Session {
public:
    explicit H1Session(domain::Bytes headerCeiling, domain::Bytes bodyCeiling,
                       domain::http::Method method = domain::http::Method::get);

    /// Build the wire bytes. `target` is the origin-form path, `authority` the
    /// Host header.
    [[nodiscard]] static std::string serialize(const domain::http::Request &request,
                                               const std::string &target,
                                               const std::string &authority,
                                               const std::string &userAgent);

    /// Feed received bytes. Returns an error once the ceiling or framing breaks.
    [[nodiscard]] std::expected<void, domain::http::HttpError> consume(const std::byte *data,
                                                                     std::size_t length);

    [[nodiscard]] bool isComplete() const noexcept { return complete_; }
    [[nodiscard]] bool isChunked() const noexcept { return chunked_; }
    /// False once the peer said `Connection: close`; such a socket is not pooled.
    [[nodiscard]] bool isReusable() const noexcept { return reusable_; }

    /// The parsed response. Only meaningful once `isComplete()`.
    [[nodiscard]] domain::http::Response take();

private:
    [[nodiscard]] std::expected<void, domain::http::HttpError> parseHead();
    [[nodiscard]] std::expected<void, domain::http::HttpError> parseBody();
    [[nodiscard]] std::expected<void, domain::http::HttpError> parseChunked();

    std::string buffer_;
    std::size_t cursor_ = 0;
    domain::http::Response response_;
    domain::Bytes headerCeiling_;
    domain::Bytes bodyCeiling_;
    std::optional<std::size_t> remaining_;
    bool chunked_ = false;
    domain::http::Method method_ = domain::http::Method::get;
    bool headParsed_ = false;
    bool complete_ = false;
    bool reusable_ = true;
    std::size_t chunkRemaining_ = 0;
    bool chunkDone_ = false;
};

} // namespace antares::adapters::http
