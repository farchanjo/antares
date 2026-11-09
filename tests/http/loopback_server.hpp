#pragma once

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace antares::test {

/// A loopback HTTP server. Test-only: it may block freely.
///
/// Each accepted connection gets its own thread, so two concurrent requests are
/// served at once instead of queueing behind one.
class LoopbackServer {
public:
    explicit LoopbackServer(std::string response)
        : responses_{std::move(response)}
    {
        start();
    }

    /// Answer successive requests from `responses`, repeating the last one.
    explicit LoopbackServer(std::vector<std::string> responses)
        : responses_(std::move(responses))
    {
        start();
    }

    ~LoopbackServer()
    {
        stopping_.store(true);
        ::shutdown(listener_, SHUT_RDWR);
        ::close(listener_);
        if (accepting_.joinable()) {
            accepting_.join();
        }

        std::vector<std::thread> workers;
        {
            const std::lock_guard<std::mutex> guard{mutex_};
            // A blocked read only returns once the peer socket is shut down.
            for (const int client : clients_) {
                ::shutdown(client, SHUT_RDWR);
            }
            workers.swap(workers_);
        }
        for (auto &worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    LoopbackServer(const LoopbackServer &) = delete;
    LoopbackServer &operator=(const LoopbackServer &) = delete;

    [[nodiscard]] std::string url() const
    {
        return "http://127.0.0.1:" + std::to_string(port_);
    }

    [[nodiscard]] std::string lastRequest() const
    {
        const std::lock_guard<std::mutex> guard{mutex_};
        return lastRequest_;
    }

private:
    void start()
    {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int reuse = 1;
        ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
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
            const std::lock_guard<std::mutex> guard{mutex_};
            clients_.push_back(client);
            workers_.emplace_back([this, client] { serve(client); });
        }
    }

    void serve(int client)
    {
        while (!stopping_.load() && readRequest(client)) {
            const std::size_t index = static_cast<std::size_t>(accepted_.fetch_add(1));
            const std::string &response =
                responses_[index < responses_.size() ? index : responses_.size() - 1];
            (void)::write(client, response.data(), response.size());
        }
        ::shutdown(client, SHUT_RDWR);
        ::close(client);
    }

    [[nodiscard]] bool readRequest(int client)
    {
        char scratch[2048];
        std::string seen;
        while (seen.find("\r\n\r\n") == std::string::npos) {
            const ssize_t got = ::read(client, scratch, sizeof scratch);
            if (got <= 0) {
                return false;
            }
            seen.append(scratch, static_cast<std::size_t>(got));
        }
        const std::lock_guard<std::mutex> guard{mutex_};
        lastRequest_ = seen;
        return true;
    }

    std::vector<std::string> responses_;
    mutable std::mutex mutex_;
    std::string lastRequest_;
    int listener_ = -1;
    std::uint16_t port_ = 0;
    std::atomic<bool> stopping_{false};
    std::atomic<int> accepted_{0};
    std::thread accepting_;
    std::vector<int> clients_;
    std::vector<std::thread> workers_;
};

} // namespace antares::test
