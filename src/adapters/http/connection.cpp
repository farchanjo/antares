#include "adapters/http/connection.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <sys/socket.h>
#include <unistd.h>

#include "adapters/http/tls_context.hpp"

namespace antares::adapters::http {
namespace {

constexpr domain::Bytes SOCKET_CHUNK{16 * 1024};

[[nodiscard]] std::expected<sockaddr_storage, domain::http::HttpError>
lookup(const std::string &host, std::uint16_t port)
{
    sockaddr_in v4{};
    v4.sin_family = AF_INET;
    v4.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &v4.sin_addr) == 1) {
        sockaddr_storage storage{};
        std::memcpy(&storage, &v4, sizeof v4);
        return storage;
    }

    sockaddr_in6 v6{};
    v6.sin6_family = AF_INET6;
    v6.sin6_port = htons(port);
    if (::inet_pton(AF_INET6, host.c_str(), &v6.sin6_addr) == 1) {
        sockaddr_storage storage{};
        std::memcpy(&storage, &v6, sizeof v6);
        return storage;
    }

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
    ready_ = std::move(ready);
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
            if (!self->url_.isTls()) {
                self->protocol_ = Protocol::http1;
                self->ready();
                return;
            }

            self->ssl_ = SSL_new(self->tls_.raw());
            self->incoming_ = BIO_new(BIO_s_mem());
            self->outgoing_ = BIO_new(BIO_s_mem());
            if (self->ssl_ == nullptr || self->incoming_ == nullptr ||
                self->outgoing_ == nullptr) {
                self->fail(domain::http::HttpError::tls);
                return;
            }

            SSL_set_bio(self->ssl_, self->incoming_, self->outgoing_);
            SSL_set_connect_state(self->ssl_);
            SSL_set_tlsext_host_name(self->ssl_, self->url_.host.c_str());
            SSL_set1_host(self->ssl_, self->url_.host.c_str());

            self->state_ = State::handshaking;
            self->driveHandshake();
        });

    if (!submitted) {
        fail(domain::http::HttpError::connect);
    }
}

void Connection::driveHandshake()
{
    const int result = SSL_do_handshake(ssl_);
    drainOutgoing();

    if (result == 1) {
        const unsigned char *selected = nullptr;
        unsigned int length = 0;
        SSL_get0_alpn_selected(ssl_, &selected, &length);
        protocol_ = (length == 2 && std::memcmp(selected, "h2", 2) == 0) ? Protocol::http2
                                                                        : Protocol::http1;
        ready();
        return;
    }

    const int error = SSL_get_error(ssl_, result);
    if (error == SSL_ERROR_WANT_READ) {
        readSocket();
        return;
    }
    if (error == SSL_ERROR_WANT_WRITE) {
        writeSocket();
        return;
    }
    fail(domain::http::HttpError::tls);
}

void Connection::ready()
{
    state_ = State::ready;
    auto callback = std::move(ready_);
    ready_ = nullptr;
    failure_ = nullptr;
    if (callback) {
        callback();
    }
    // Anything that arrived with the handshake is already decrypted.
    drainPlaintext();
}

void Connection::readSocket()
{
    auto self = shared_from_this();
    auto submitted = engine_.submitRead(handle_, std::nullopt, SOCKET_CHUNK,
                                        [self](auto chunk) { self->onSocketRead(std::move(chunk)); });
    if (!submitted) {
        fail(domain::http::HttpError::closed);
    }
}

void Connection::writeSocket()
{
    drainOutgoing();
}

void Connection::onSocketRead(std::expected<ports::io::Buffer, ports::io::IoError> chunk)
{
    if (state_ == State::closed) {
        return;
    }
    if (!chunk) {
        fail(chunk.error() == ports::io::IoError::closed ? domain::http::HttpError::closed
                                                         : domain::http::HttpError::protocol);
        return;
    }

    if (ssl_ != nullptr) {
        (void)BIO_write(incoming_, chunk->data(), static_cast<int>(chunk->size()));
        if (state_ == State::handshaking) {
            driveHandshake();
            return;
        }
        drainPlaintext();
        drainOutgoing();
        return;
    }

    plain_.append(reinterpret_cast<const char *>(chunk->data()), chunk->size());
    drainPlaintext();
}

void Connection::drainPlaintext()
{
    if (ssl_ == nullptr) {
        if (plain_.empty() || !handler_) {
            return;
        }
        std::string swap;
        swap.swap(plain_);
        handler_(reinterpret_cast<const std::byte *>(swap.data()), swap.size());
        if (state_ == State::ready) {
            readSocket();
        }
        return;
    }

    std::uint8_t buffer[16 * 1024];
    while (true) {
        const int got = SSL_read(ssl_, buffer, sizeof buffer);
        if (got > 0) {
            if (handler_) {
                handler_(reinterpret_cast<const std::byte *>(buffer), static_cast<std::size_t>(got));
            }
            continue;
        }
        const int error = SSL_get_error(ssl_, got);
        if (error == SSL_ERROR_WANT_READ) {
            readSocket();
            return;
        }
        if (error == SSL_ERROR_WANT_WRITE) {
            drainOutgoing();
            continue;
        }
        fail(domain::http::HttpError::closed);
        return;
    }
}

void Connection::write(ports::io::Buffer bytes, WriteDone done)
{
    if (state_ == State::closed) {
        done(WriteOutcome{.bytes = 0, .error = domain::http::HttpError::closed, .ok = false});
        return;
    }

    if (ssl_ == nullptr) {
        writeQueue_.push_back(std::move(done));
        auto self = shared_from_this();
        auto submitted = engine_.submitWrite(handle_, std::nullopt, std::move(bytes),
                                             [self](auto wrote) { self->onSocketWritten(wrote); });
        if (!submitted) {
            fail(domain::http::HttpError::closed);
        }
        return;
    }

    writeQueue_.push_back(std::move(done));
    const int put = SSL_write(ssl_, bytes.data(), static_cast<int>(bytes.size()));
    std::fprintf(stderr, "  ssl_write %d of %zu\n", put, bytes.size());
    drainOutgoing();
}

void Connection::drainOutgoing()
{
    std::fprintf(stderr, "  drain writing=%d pending=%ld\n", static_cast<int>(writing_),
                 outgoing_ == nullptr ? -1L : static_cast<long>(BIO_ctrl_pending(outgoing_)));
    if (outgoing_ == nullptr || writing_) {
        return;
    }

    const std::size_t pending = static_cast<std::size_t>(BIO_ctrl_pending(outgoing_));
    if (pending == 0) {
        if (!writeQueue_.empty() && state_ == State::ready) {
            std::deque<WriteDone> queue;
            queue.swap(writeQueue_);
            for (auto &done : queue) {
                done(WriteOutcome{.bytes = pending, .ok = true});
            }
        }
        return;
    }

    writeBuffer_.assign(pending, '\0');
    (void)BIO_read(outgoing_, writeBuffer_.data(), static_cast<int>(pending));

    writing_ = true;
    auto self = shared_from_this();
    auto submitted = engine_.submitWrite(
        handle_, std::nullopt,
        ports::io::Buffer{reinterpret_cast<const std::byte *>(writeBuffer_.data()),
                          reinterpret_cast<const std::byte *>(writeBuffer_.data()) +
                              writeBuffer_.size()},
        [self](auto wrote) { self->onSocketWritten(wrote); });
    if (!submitted) {
        fail(domain::http::HttpError::closed);
    }
}

void Connection::onSocketWritten(std::expected<std::size_t, ports::io::IoError> wrote)
{
    std::fprintf(stderr, "  wrote ok=%d\n", static_cast<int>(wrote.has_value()));
    writing_ = false;
    if (!wrote) {
        fail(domain::http::HttpError::closed);
        return;
    }
    drainOutgoing();
}

void Connection::close()
{
    state_ = State::closed;
    if (ssl_ != nullptr) {
        SSL_free(ssl_);
        ssl_ = nullptr;
        incoming_ = nullptr;
        outgoing_ = nullptr;
    }
    handler_ = nullptr;
}

void Connection::fail(domain::http::HttpError error)
{
    if (state_ == State::closed) {
        return;
    }
    close();

    auto callback = std::move(failure_);
    failure_ = nullptr;
    if (callback) {
        callback(error);
    }
}

} // namespace antares::adapters::http
