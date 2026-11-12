#include "adapters/http/connection.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <netdb.h>
#include <expected>
#include <optional>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <sys/socket.h>
#include <unistd.h>

#include "adapters/http/tls_context.hpp"

namespace antares::adapters::http {
namespace {

constexpr domain::Bytes SOCKET_CHUNK{std::size_t{16} * 1024};

/// One place where a transport error becomes an HTTP one. `cancelled` is kept
/// apart from `protocol`: the peer did nothing wrong, a caller withdrew
/// (FR-005 of 004).
[[nodiscard]] domain::http::HttpError httpErrorFrom(ports::io::IoError error) noexcept
{
    switch (error) {
    case ports::io::IoError::closed:
        return domain::http::HttpError::closed;
    case ports::io::IoError::cancelled:
        return domain::http::HttpError::cancelled;
    default:
        return domain::http::HttpError::protocol;
    }
}

/// Literal addresses never leave the process; a name goes through the engine,
/// so the loop keeps running while the resolver works.
[[nodiscard]] std::optional<sockaddr_storage> literal(const std::string &host, std::uint16_t port)
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
    return std::nullopt;
}

} // namespace

Connection::Connection(ports::io::IoEngine &engine, const TlsContext &tls,
                       ports::log::Logger *logger)
    : engine_(engine)
    , tls_(tls)
    , logger_(logger)
{
}

void Connection::note(ports::log::Level level, std::string_view message) const
{
    if (logger_ != nullptr) {
        logger_->write(level, "http", message);
    }
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

    auto self = shared_from_this();

    if (auto direct = literal(url.host, url.port)) {
        begin(*direct);
        return;
    }

    auto submittedResolve = engine_.submitResolve(
        url_.host, url_.port, [self](ports::io::IoEngine::ResolveResult resolved) {
            if (!resolved) {
                self->fail(domain::http::HttpError::connect);
                return;
            }
            self->begin(*resolved);
        });
    if (!submittedResolve) {
        fail(domain::http::HttpError::connect);
    }
}

void Connection::begin(const sockaddr_storage &address)
{
    if (state_ == State::closed) {
        return;
    }

    const int family = reinterpret_cast<const sockaddr *>(&address)->sa_family;
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

    const auto *raw = reinterpret_cast<const std::byte *>(&address);
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
            // An IP literal is checked as an address; SSL_set1_host would treat
            // it as a DNS name and never match the SAN.
            sockaddr_in probe{};
            if (::inet_pton(AF_INET, self->url_.host.c_str(), &probe.sin_addr) == 1 ||
                self->url_.host.find(':') != std::string::npos) {
                (void)X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(self->ssl_),
                                                    self->url_.host.c_str());
            } else {
                SSL_set1_host(self->ssl_, self->url_.host.c_str());
            }

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
    if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
        note(ports::log::Level::warn,
             "handshake failed err=" + std::to_string(error) +
                 " verify=" + std::to_string(SSL_get_verify_result(ssl_)));
    }
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
    // Anything that arrived with the handshake is already decrypted, so drain
    // first and only then ask for more.
    drainPlaintext();
    readSocket();
}

void Connection::readSocket()
{
    if (reading_ || state_ == State::closed) {
        return;
    }
    reading_ = true;
    auto self = shared_from_this();
    auto submitted = engine_.submitRead(handle_, std::nullopt, SOCKET_CHUNK,
                                        [self](auto chunk) { self->onSocketRead(std::move(chunk)); });
    if (!submitted) {
        reading_ = false;
        fail(domain::http::HttpError::closed);
    }
}

void Connection::writeSocket()
{
    drainOutgoing();
}

void Connection::onSocketRead(std::expected<ports::io::Buffer, ports::io::IoError> chunk)
{
    reading_ = false;
    if (state_ == State::closed) {
        return;
    }
    if (!chunk) {
        fail(httpErrorFrom(chunk.error()));
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
        // Nothing arms the next read on its own: a stream that is still open
        // would stop at the first chunk, which is every watch and every exec.
        readSocket();
        return;
    }

    plain_.append(reinterpret_cast<const char *>(chunk->data()), chunk->size());
    drainPlaintext();
    readSocket();
}

void Connection::drainPlaintext()
{
    // A handler may replace itself (a redirect re-enters), so keep a copy on
    // the stack instead of calling through the member.
    auto handler = handler_;

    if (ssl_ == nullptr) {
        if (plain_.empty() || !handler) {
            return;
        }
        std::string swap;
        swap.swap(plain_);
        handler(reinterpret_cast<const std::byte *>(swap.data()), swap.size());
        if (state_ == State::ready) {
            readSocket();
        }
        return;
    }

    note(ports::log::Level::trace,
         "plaintext handler=" + std::to_string(static_cast<int>(handler_ != nullptr)));
    std::uint8_t buffer[16 * 1024];
    while (true) {
        const int got = SSL_read(ssl_, buffer, sizeof buffer);
        if (got > 0) {
            if (handler) {
                handler(reinterpret_cast<const std::byte *>(buffer), static_cast<std::size_t>(got));
            }
            continue;
        }
        const int error = SSL_get_error(ssl_, got);
        note(ports::log::Level::trace,
             "ssl_read got=" + std::to_string(got) + " err=" + std::to_string(error) +
                 " init=" + std::to_string(SSL_is_init_finished(ssl_)));
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
        // No TLS: the caller's completion is the socket completion.
        auto self = shared_from_this();
        // submitWrite may refuse; the caller's completion must survive the
        // refusal, so the lambda captures a copy of `done` and not `done`.
        auto completed = done;
        auto submitted = engine_.submitWrite(
            handle_, std::nullopt, std::move(bytes),
            [self, done = std::move(completed)](auto wrote) {
                if (wrote) {
                    done(WriteOutcome{.bytes = *wrote, .ok = true});
                    return;
                }
                done(WriteOutcome{});
                self->fail(domain::http::HttpError::closed);
            });
        if (!submitted) {
            done(WriteOutcome{});
            fail(domain::http::HttpError::closed);
        }
        return;
    }

    writeQueue_.push_back(std::move(done));
    const int put = SSL_write(ssl_, bytes.data(), static_cast<int>(bytes.size()));
    note(ports::log::Level::trace, "ssl_write " + std::to_string(put) + " of " +
                                       std::to_string(bytes.size()));
    drainOutgoing();
}

void Connection::drainOutgoing()
{
    note(ports::log::Level::trace,
         "drain writing=" + std::to_string(static_cast<int>(writing_)) + " pending=" +
             std::to_string(outgoing_ == nullptr ? -1L
                                                 : static_cast<long>(BIO_ctrl_pending(outgoing_))));
    if (outgoing_ == nullptr || writing_) {
        return;
    }

    const auto pending = static_cast<std::size_t>(BIO_ctrl_pending(outgoing_));
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
    note(ports::log::Level::trace, wrote.has_value() ? "wrote ok" : "write failed");
    writing_ = false;
    if (!wrote) {
        fail(domain::http::HttpError::closed);
        return;
    }
    drainOutgoing();
}

void Connection::cancelPending()
{
    if (state_ == State::closed) {
        return;
    }
    engine_.cancelPending(handle_);
}

void Connection::close()
{
    // Idempotent: the client may close on the loop right before the destructor
    // runs from a foreign thread, and the SSL objects must be freed once.
    if (state_ == State::closed) {
        return;
    }
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
