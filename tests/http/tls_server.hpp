#pragma once

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <nghttp2/nghttp2.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "test_cert.hpp"

namespace antares::test {

/// TLS loopback server that offers exactly the ALPN protocols asked for.
///
/// Test-only. It answers HTTP/1.1 and, when `h2` is true, speaks HTTP/2 so a
/// case can prove negotiation, fallback and multiplexing.
class TlsServer {
public:
    TlsServer(std::string body, bool h2)
        : body_(std::move(body))
        , offerH2_(h2)
    {
        context_ = SSL_CTX_new(TLS_server_method());
        SSL_CTX_use_certificate_chain_file(context_, nullptr);
        BIO *certBio = BIO_new_mem_buf(TEST_CERT, -1);
        X509 *certificate = PEM_read_bio_X509(certBio, nullptr, nullptr, nullptr);
        SSL_CTX_use_certificate(context_, certificate);
        X509_free(certificate);
        BIO_free(certBio);

        BIO *keyBio = BIO_new_mem_buf(TEST_KEY, -1);
        EVP_PKEY *key = PEM_read_bio_PrivateKey(keyBio, nullptr, nullptr, nullptr);
        SSL_CTX_use_PrivateKey(context_, key);
        EVP_PKEY_free(key);
        BIO_free(keyBio);

        SSL_CTX_set_alpn_select_cb(context_, &select, this);
        start();
    }

    ~TlsServer()
    {
        stopping_.store(true);
        ::shutdown(listener_, SHUT_RDWR);
        ::close(listener_);
        if (accepting_.joinable()) {
            accepting_.join();
        }
        std::vector<std::thread> workers;
        std::vector<int> clients;
        {
            const std::lock_guard<std::mutex> guard{mutex_};
            workers.swap(workers_);
            clients.swap(clients_);
        }
        for (const int client : clients) {
            ::shutdown(client, SHUT_RDWR);
        }
        for (auto &worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        SSL_CTX_free(context_);
    }

    TlsServer(const TlsServer &) = delete;
    TlsServer &operator=(const TlsServer &) = delete;

    [[nodiscard]] std::string url() const { return "https://127.0.0.1:" + std::to_string(port_); }
    [[nodiscard]] int connections() const noexcept { return accepted_.load(); }
    [[nodiscard]] int requests() const noexcept { return requests_.load(); }
    [[nodiscard]] int maxConcurrentStreams() const noexcept { return peakStreams_.load(); }

private:
    static int select(SSL *, const unsigned char **out, unsigned char *outLength,
                      const unsigned char *in, unsigned int inLength, void *userData)
    {
        auto *self = static_cast<TlsServer *>(userData);
        // Without the leading length byte: `outLength` is the name size.
        static constexpr unsigned char H2[] = {'h', '2'};
        static constexpr unsigned char H11[] = {'h', 't', 't', 'p', '/', '1', '.', '1'};

        for (unsigned int index = 0; index + 1 <= inLength;) {
            const unsigned int length = in[index];
            const unsigned char *candidate = in + index + 1;
            if (length == 2 && std::memcmp(candidate, "h2", 2) == 0 && self->offerH2_) {
                *out = H2;
                *outLength = sizeof H2;
                return SSL_TLSEXT_ERR_OK;
            }
            index += length + 1;
        }
        *out = H11;
        *outLength = sizeof H11;
        return SSL_TLSEXT_ERR_OK;
    }

    void start()
    {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int reuse = 1;
        ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof address);
        ::listen(listener_, 16);

        socklen_t length = sizeof address;
        ::getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &length);
        port_ = ntohs(address.sin_port);

        accepting_ = std::thread([this] { acceptLoop(); });
    }

    void acceptLoop()
    {
        while (!stopping_.load()) {
            const int client = ::accept(listener_, nullptr, nullptr);
            if (client < 0) {
                return;
            }
            ++accepted_;
            const std::lock_guard<std::mutex> guard{mutex_};
            clients_.push_back(client);
            workers_.emplace_back([this, client] { serve(client); });
        }
    }

    void serve(int client)
    {
        SSL *ssl = SSL_new(context_);
        SSL_set_fd(ssl, client);
        const int accepted = SSL_accept(ssl);
        if (accepted != 1) {
            std::fprintf(stderr, "  server handshake %d\n", SSL_get_error(ssl, accepted));
            ERR_print_errors_fp(stderr);
        }
        if (accepted == 1) {
            const unsigned char *selected = nullptr;
            unsigned int length = 0;
            SSL_get0_alpn_selected(ssl, &selected, &length);
            const bool h2 = length == 2 && std::memcmp(selected, "h2", 2) == 0;
            std::fprintf(stderr, "  server alpn h2=%d\n", static_cast<int>(h2));
            if (h2) {
                serveHttp2(ssl);
            } else {
                serveHttp1(ssl);
            }
        }
        SSL_shutdown(ssl);
        SSL_free(ssl);
        ::shutdown(client, SHUT_RDWR);
        ::close(client);
    }

    void serveHttp1(SSL *ssl)
    {
        char scratch[4096];
        while (!stopping_.load()) {
            std::string seen;
            while (seen.find("\r\n\r\n") == std::string::npos) {
                const int got = SSL_read(ssl, scratch, sizeof scratch);
                if (got <= 0) {
                    return;
                }
                seen.append(scratch, static_cast<std::size_t>(got));
            }
            ++requests_;
            const std::string response = "HTTP/1.1 200 OK\r\nContent-Length: " +
                                         std::to_string(body_.size()) +
                                         "\r\nConnection: keep-alive\r\n\r\n" + body_;
            (void)SSL_write(ssl, response.data(), static_cast<int>(response.size()));
        }
    }

    static ssize_t sendCallback(nghttp2_session *, const std::uint8_t *data, std::size_t length,
                                int, void *userData)
    {
        auto *ssl = static_cast<SSL *>(userData);
        return static_cast<ssize_t>(SSL_write(ssl, data, static_cast<int>(length)));
    }

    void serveHttp2(SSL *ssl)
    {
        nghttp2_session_callbacks *callbacks = nullptr;
        nghttp2_session_callbacks_new(&callbacks);

        nghttp2_session_callbacks_set_send_callback(callbacks, &sendCallback);
        nghttp2_session_callbacks_set_on_begin_headers_callback(
            callbacks, [](nghttp2_session *, const nghttp2_frame *frame, void *userData) -> int {
                if (frame->hd.type == NGHTTP2_HEADERS) {
                    auto *self = static_cast<TlsServer *>(userData);
                    self->active_.fetch_add(1);
                    ++self->requests_;
                    const int now = self->active_.load();
                    if (now > self->peakStreams_.load()) {
                        self->peakStreams_.store(now);
                    }
                }
                return 0;
            });
        nghttp2_session_callbacks_set_on_stream_close_callback(
            callbacks, [](nghttp2_session *, std::int32_t, std::uint32_t, void *userData) -> int {
                static_cast<TlsServer *>(userData)->active_.fetch_sub(1);
                return 0;
            });
        nghttp2_session_callbacks_set_on_frame_recv_callback(
            callbacks, [](nghttp2_session *session, const nghttp2_frame *frame, void *userData) -> int {
                std::fprintf(stderr, "  server frame type=%d\n", frame->hd.type);
                if (frame->hd.type != NGHTTP2_HEADERS ||
                    frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
                    return 0;
                }
                auto *self = static_cast<TlsServer *>(userData);
                static thread_local std::string statusName = ":status";
                static thread_local std::string statusValue = "200";
                static thread_local std::string typeName = "content-type";
                static thread_local std::string typeValue = "text/plain";

                nghttp2_nv headers[2] = {};
                headers[0].name = reinterpret_cast<std::uint8_t *>(statusName.data());
                headers[0].namelen = statusName.size();
                headers[0].value = reinterpret_cast<std::uint8_t *>(statusValue.data());
                headers[0].valuelen = statusValue.size();
                headers[1].name = reinterpret_cast<std::uint8_t *>(typeName.data());
                headers[1].namelen = typeName.size();
                headers[1].value = reinterpret_cast<std::uint8_t *>(typeValue.data());
                headers[1].valuelen = typeValue.size();

                nghttp2_data_provider provider{};
                provider.source.ptr = &self->body_;
                provider.read_callback = [](nghttp2_session *, std::int32_t, std::uint8_t *buffer,
                                            std::size_t length, std::uint32_t *flags,
                                            nghttp2_data_source *source, void *) -> ssize_t {
                    auto *body = static_cast<std::string *>(source->ptr);
                    const std::size_t take = std::min(body->size(), length);
                    std::memcpy(buffer, body->data(), take);
                    *flags |= NGHTTP2_DATA_FLAG_EOF;
                    return static_cast<ssize_t>(take);
                };

                (void)nghttp2_submit_response(session, frame->hd.stream_id, headers, 2, &provider);
                (void)nghttp2_session_send(session);
                return 0;
            });

        nghttp2_session *session = nullptr;
        nghttp2_session_server_new(&session, callbacks, this);
        nghttp2_session_callbacks_del(callbacks);

        if (session != nullptr) {
            std::fprintf(stderr, "  [s] settings\n");
            (void)nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, nullptr, 0);
            std::fprintf(stderr, "  [s] sent=%d\n", static_cast<int>(nghttp2_session_send(session)));

            std::uint8_t scratch[16384];
            while (!stopping_.load()) {
                std::fprintf(stderr, "  [s] reading\n");
                const int got = SSL_read(ssl, scratch, sizeof scratch);
                std::fprintf(stderr, "  [s] read %d\n", got);
                if (got <= 0) {
                    break;
                }
                if (nghttp2_session_mem_recv(session, scratch, static_cast<std::size_t>(got)) < 0) {
                    break;
                }
                (void)nghttp2_session_send(session);
            }
            nghttp2_session_del(session);
        }
    }

    std::string body_;
    bool offerH2_ = false;
    SSL_CTX *context_ = nullptr;
    int listener_ = -1;
    std::uint16_t port_ = 0;
    mutable std::mutex mutex_;
    std::atomic<bool> stopping_{false};
    std::atomic<int> accepted_{0};
    std::atomic<int> requests_{0};
    std::atomic<int> active_{0};
    std::atomic<int> peakStreams_{0};
    std::thread accepting_;
    std::vector<int> clients_;
    std::vector<std::thread> workers_;
};

} // namespace antares::test
