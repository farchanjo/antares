#pragma once

#include <memory>
#include <string>

#include <openssl/ssl.h>

#include "domain/http/http_error.hpp"

namespace antares::adapters::http {

/// Owns the process-wide TLS configuration: trust store, protocol floor, ALPN
/// order. One context serves every connection (FR-009).
class TlsContext {
public:
    /// Build the context. Returns nullptr on failure; the caller maps it.
    [[nodiscard]] static std::unique_ptr<TlsContext> create(bool verifyPeer);

    ~TlsContext();
    TlsContext(const TlsContext &) = delete;
    TlsContext &operator=(const TlsContext &) = delete;
    TlsContext(TlsContext &&) = delete;
    TlsContext &operator=(TlsContext &&) = delete;

    [[nodiscard]] SSL_CTX *raw() noexcept { return context_; }

    /// True when the last verification failure came from the trust store.
    [[nodiscard]] static domain::http::HttpError lastError() noexcept;

private:
    explicit TlsContext(SSL_CTX *context);

    SSL_CTX *context_ = nullptr;
};

} // namespace antares::adapters::http
