#pragma once

#include <atomic>
#include <cstring>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace antares::test {

/// A one-thread HTTP server on loopback. Test-only: it may block freely.
class LoopbackServer {
public:
    explicit LoopbackServer(std::string response)
        : response_(std::move(response))
    {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int reuse = 1;
        ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        ::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof address);
        ::listen(listener_, 8);

        socklen_t length = sizeof address;
        ::getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &length);
        port_ = ntohs(address.sin_port);

        worker_ = std::thread([this] { serve(); });
    }

    ~LoopbackServer()
    {
        stopping_.store(true);
        ::shutdown(listener_, SHUT_RDWR);
        ::close(listener_);
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    LoopbackServer(const LoopbackServer &) = delete;
    LoopbackServer &operator=(const LoopbackServer &) = delete;

    [[nodiscard]] std::string url() const
    {
        return "http://127.0.0.1:" + std::to_string(port_);
    }

    [[nodiscard]] int accepted() const noexcept { return accepted_.load(); }

private:
    void serve()
    {
        while (!stopping_.load()) {
            const int client = ::accept(listener_, nullptr, nullptr);
            if (client < 0) {
                return;
            }
            ++accepted_;
            readRequest(client);
            (void)::write(client, response_.data(), response_.size());
            ::close(client);
        }
    }

    void readRequest(int client)
    {
        char scratch[2048];
        std::string seen;
        while (seen.find("\r\n\r\n") == std::string::npos) {
            const ssize_t got = ::read(client, scratch, sizeof scratch);
            if (got <= 0) {
                return;
            }
            seen.append(scratch, static_cast<std::size_t>(got));
        }
        lastRequest_ = seen;
    }

public:
    [[nodiscard]] const std::string &lastRequest() const noexcept { return lastRequest_; }

private:
    std::string response_;
    std::string lastRequest_;
    int listener_ = -1;
    std::uint16_t port_ = 0;
    std::atomic<bool> stopping_{false};
    std::atomic<int> accepted_{0};
    std::thread worker_;
};

} // namespace antares::test
