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
    /// `http2` offers h2 first; false offers only http/1.1 so an h1-only
    /// client never negotiates a protocol it cannot frame.
    /// `clientCertificate` is loaded when the auth kind is mutual TLS.
    [[nodiscard]] static std::unique_ptr<TlsContext> create(bool verifyPeer, bool http2,
                                                            const domain::http::Auth &auth);

    ~TlsContext();
    TlsContext(const TlsContext &) = delete;
    TlsContext &operator=(const TlsContext &) = delete;
    TlsContext(TlsContext &&) = delete;
    TlsContext &operator=(TlsContext &&) = delete;

    [[nodiscard]] SSL_CTX *raw() const noexcept { return context_; }

    /// True when the last verification failure came from the trust store.
    [[nodiscard]] static domain::http::HttpError lastError() noexcept;

private:
    explicit TlsContext(SSL_CTX *context);

    SSL_CTX *context_ = nullptr;
};

} // namespace antares::adapters::http
