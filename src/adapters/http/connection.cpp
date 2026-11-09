#include "adapters/http/connection.hpp"

#include <cstring>

#include <netdb.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <sys/socket.h>
#include <unistd.h>

#include "adapters/http/tls_context.hpp"

namespace antares::adapters::http {
namespace {

/// Resolve once, synchronously. The engine has no resolver yet, so this is the
/// one blocking call left; it happens before any engine operation is submitted.
[[nodiscard]] std::expected<sockaddr_storage, domain::http::HttpError>
lookup(const std::string &host, std::uint16_t port)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *found = nullptr;
    const std::string service = std::to_string(port);
    if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &found) != 0 || found == nullptr) {
        return std::unexpected(domain::http::HttpError::connect);
    }

    sockaddr_storage storage{};
    std::memcpy(&storage, found->ai_addr, found->ai_addrlen);
    ::freeaddrinfo(found);
    return storage;
}

} // namespace

Connection::Connection(ports::io::IoEngine &engine, const TlsContext &tls)
    : engine_(engine)
    , tls_(tls)
{
}

Connection::~Connection()
{
    close();
}

void Connection::open(const domain::http::Url &url, Ready ready, Failure failure)
{
    url_ = url;
    failure_ = std::move(failure);

    auto address = lookup(url.host, url.port);
    if (!address) {
        fail(address.error());
        return;
    }

    const int family = reinterpret_cast<const sockaddr *>(&*address)->sa_family;
    const int descriptor = ::socket(family, SOCK_STREAM, 0);
    if (descriptor < 0) {
        fail(domain::http::HttpError::connect);
        return;
    }

    auto adopted = ports::io::IoHandle::adopt(descriptor);
    if (!adopted) {
        ::close(descriptor);
        fail(domain::http::HttpError::connect);
        return;
    }
    handle_ = std::move(*adopted);
    state_ = State::connecting;

    const auto *raw = reinterpret_cast<const std::byte *>(&*address);
    const std::size_t length = family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);

    auto self = shared_from_this();
    auto submitted = engine_.submitConnect(
        handle_, std::span<const std::byte>{raw, length},
        [self](std::expected<void, ports::io::IoError> result) {
            if (!result) {
                self->fail(domain::http::HttpError::connect);
                return;
            }
            self->afterConnected();
        });

    if (!submitted) {
        fail(submitted.error() == ports::io::IoError::tooManyDescriptors
                 ? domain::http::HttpError::connect
                 : domain::http::HttpError::connect);
    }
}

void Connection::afterConnected()
{
    if (url_.isTls()) {
        ssl_ = SSL_new(tls_.raw());
        if (ssl_ == nullptr) {
            fail(domain::http::HttpError::tls);
            return;
        }
        SSL_set_fd(ssl_, handle_.native());
        SSL_set_tlsext_host_name(ssl_, url_.host.c_str());
        SSL_set1_host(ssl_, url_.host.c_str());

        state_ = State::handshaking;
        afterHandshake(SSL_connect(ssl_));
        return;
    }

    state_ = State::ready;
}

void Connection::afterHandshake(int result)
{
    if (result == 1) {
        const unsigned char *selected = nullptr;
        unsigned int length = 0;
        SSL_get0_alpn_selected(ssl_, &selected, &length);
        http2_ = length == 2 && std::memcmp(selected, "h2", 2) == 0;
        state_ = State::ready;
        return;
    }

    const int error = SSL_get_error(ssl_, result);
    if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
        fail(domain::http::HttpError::tls);
        return;
    }

    // The handshake needs the socket; drive it from the engine.
    auto self = shared_from_this();
    const bool wantsRead = error == SSL_ERROR_WANT_READ;
    auto done = [self](std::expected<ports::io::Buffer, ports::io::IoError>) {
        self->afterHandshake(SSL_connect(self->ssl_));
    };

    if (wantsRead) {
        (void)engine_.submitRead(handle_, std::nullopt, domain::Bytes{0}, done);
    }
}

void Connection::write(ports::io::Buffer bytes, WriteDone done)
{
    auto self = shared_from_this();
    (void)engine_.submitWrite(
        handle_, std::nullopt, std::move(bytes),
        [self, done = std::move(done)](std::expected<std::size_t, ports::io::IoError> result) {
            if (!result) {
                done(std::unexpected(domain::http::HttpError::closed));
                return;
            }
            done(*result);
        });
}

void Connection::read(domain::Bytes maxBytes, ReadDone done)
{
    auto self = shared_from_this();
    (void)engine_.submitRead(
        handle_, std::nullopt, maxBytes,
        [done = std::move(done)](std::expected<ports::io::Buffer, ports::io::IoError> result) {
            if (!result) {
                done(std::unexpected(result.error() == ports::io::IoError::closed
                                         ? domain::http::HttpError::closed
                                         : domain::http::HttpError::protocol));
                return;
            }
            done(std::move(*result));
        });
}

void Connection::close()
{
    if (ssl_ != nullptr) {
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    state_ = State::closed;
}

void Connection::fail(domain::http::HttpError error)
{
    close();
    if (failure_) {
        auto callback = std::move(failure_);
        failure_ = nullptr;
        callback(error);
    }
}

} // namespace antares::adapters::http
