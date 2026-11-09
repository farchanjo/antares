#include "adapters/http/tls_context.hpp"

#include <algorithm>

#include <openssl/err.h>
#include <openssl/x509v3.h>

namespace antares::adapters::http {
namespace {

/// ALPN order decides the outcome: h2 first, http/1.1 as the fallback (FR-005).
int selectProtocol(SSL * /*ssl*/, const unsigned char **out, unsigned char *outLength,
                   const unsigned char *in, unsigned int inLength, void * /*arg*/)
{
    static constexpr unsigned char H2[] = {2, 'h', '2'};
    static constexpr unsigned char H11[] = {8, 'h', 't', 't', 'p', '/', '1', '.', '1'};

    for (unsigned int index = 0; index < inLength;) {
        const unsigned int length = in[index];
        const unsigned char *candidate = in + index + 1;

        if (length == H2[0] && std::equal(candidate, candidate + length, H2 + 1)) {
            *out = H2;
            *outLength = sizeof H2;
            return SSL_TLSEXT_ERR_OK;
        }
        if (length == H11[0] && std::equal(candidate, candidate + length, H11 + 1)) {
            *out = H11;
            *outLength = sizeof H11;
            return SSL_TLSEXT_ERR_OK;
        }
        index += length + 1;
    }
    return SSL_TLSEXT_ERR_ALERT_FATAL;
}

} // namespace

TlsContext::TlsContext(SSL_CTX *context)
    : context_(context)
{
}

TlsContext::~TlsContext()
{
    if (context_ != nullptr) {
        SSL_CTX_free(context_);
    }
}

std::unique_ptr<TlsContext> TlsContext::create(bool verifyPeer, bool http2)
{
    SSL_CTX *context = SSL_CTX_new(TLS_client_method());
    if (context == nullptr) {
        return nullptr;
    }

    SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION);
    // h2 first, http/1.1 second: the server picks, and a server without h2
    // still answers on 1.1 instead of failing the handshake.
    static constexpr unsigned char OFFER[] = {2, 'h', '2', 8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
    static constexpr unsigned char OFFER_H1[] = {8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
    if (http2) {
        SSL_CTX_set_alpn_protos(context, OFFER, sizeof OFFER);
    } else {
        SSL_CTX_set_alpn_protos(context, OFFER_H1, sizeof OFFER_H1);
    }
    SSL_CTX_set_mode(context, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

    if (verifyPeer) {
        SSL_CTX_set_verify(context, SSL_VERIFY_PEER, nullptr);
        if (SSL_CTX_set_default_verify_paths(context) != 1) {
            SSL_CTX_free(context);
            return nullptr;
        }
    } else {
        SSL_CTX_set_verify(context, SSL_VERIFY_NONE, nullptr);
    }

    return std::unique_ptr<TlsContext>{new TlsContext{context}};
}

domain::http::HttpError TlsContext::lastError() noexcept
{
    const unsigned long code = ERR_peek_last_error();
    if (code == 0) {
        return domain::http::HttpError::tls;
    }

    const int reason = ERR_GET_REASON(code);
    if (reason == X509_V_ERR_HOSTNAME_MISMATCH || reason == X509_V_ERR_CERT_HAS_EXPIRED ||
        reason == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY ||
        reason == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN) {
        return domain::http::HttpError::tls;
    }
    return domain::http::HttpError::tls;
}

} // namespace antares::adapters::http
