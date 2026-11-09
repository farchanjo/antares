// Conformance suite for the HTTP client (feature 002).
//
// One case per process so CTest fans the suite across cores; each case builds its
// own engine, client and loopback server.

#include <atomic>
#include <expected>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "adapters/http/client.hpp"
#include "application/http/blocking_client.hpp"
#include "application/io/engine_factory.hpp"
#include "loopback_server.hpp"

namespace {

using antares::adapters::http::HttpClient;
using antares::application::http::BlockingClient;
using antares::application::io::makeEngine;
using antares::domain::http::ClientConfig;
using antares::domain::http::HttpError;
using antares::domain::http::Method;
using antares::domain::http::Request;
using antares::domain::http::Response;
using antares::ports::io::IoEngine;

int failures = 0;
int checks = 0;

void check(bool condition, const char *what)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "  FAIL %s\n", what);
    }
}

class Stack {
public:
    Stack()
    {
        auto built = makeEngine();
        if (!built) {
            return;
        }
        engine_ = std::move(*built);
        // The loop owns its own thread here: cases run on the caller thread and
        // may block, which is exactly what the blocking facade is for.
        worker_ = std::thread([this] { engine_->run(); });
        config_.verifyPeer = false;
        client_ = std::make_shared<HttpClient>(*engine_, config_);
        blocking_ = std::make_unique<BlockingClient>(*client_, *engine_);
    }

    ~Stack()
    {
        client_.reset();
        if (engine_) {
            engine_->stop();
        }
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    Stack(const Stack &) = delete;
    Stack &operator=(const Stack &) = delete;

    [[nodiscard]] bool ok() const noexcept { return engine_ != nullptr; }
    [[nodiscard]] HttpClient &client() noexcept { return *client_; }
    [[nodiscard]] BlockingClient &blocking() noexcept { return *blocking_; }
    [[nodiscard]] IoEngine &engine() noexcept { return *engine_; }
    [[nodiscard]] ClientConfig &config() noexcept { return config_; }

private:
    std::unique_ptr<IoEngine> engine_;
    ClientConfig config_{};
    std::thread worker_;
    std::shared_ptr<HttpClient> client_;
    std::unique_ptr<BlockingClient> blocking_;
};

Request get(const std::string &url)
{
    Request request;
    request.method = Method::get;
    request.url = url;
    return request;
}

void caseAsyncGet()
{
    Stack stack;
    check(stack.ok(), "engine built");
    if (!stack.ok()) {
        return;
    }

    antares::test::LoopbackServer server{
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello"};

    std::atomic<bool> done{false};
    Response received;
    HttpError failure = HttpError::protocol;

    auto ticket = stack.client().submit(get(server.url() + "/"), [&](auto result) {
        if (result) {
            received = std::move(*result);
        } else {
            failure = result.error();
        }
        done.store(true);
    });
    check(ticket.has_value(), "submit accepted");

    for (int spin = 0; spin < 800 && !done.load(); ++spin) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    check(done.load(), "completion fired");
    check(received.status == 200, "status 200");
    check(received.body.size() == 5, "body carried");
    check(failure == HttpError::protocol || received.status == 200, "no failure");
    check(server.lastRequest().rfind("GET / HTTP/1.1", 0) == 0, "request line");
}

void caseSyncGet()
{
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    antares::test::LoopbackServer server{
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok"};

    auto result = stack.blocking().send(get(server.url() + "/sync"));
    check(result.has_value(), "sync call returned a response");
    if (result) {
        check(result->status == 200, "status 200");
        check(result->body.size() == 2, "body carried");
    }
}

void caseRedirectFollowed()
{
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    antares::test::LoopbackServer server{std::vector<std::string>{
        "HTTP/1.1 302 Found\r\nLocation: /final\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok"}};

    auto result = stack.blocking().send(get(server.url() + "/start"));
    check(result.has_value(), "redirect returned");
    if (result) {
        check(result->hops == 1, "one hop recorded");
        check(server.lastRequest().rfind("GET /final HTTP/1.1", 0) == 0, "location followed");
    }
}

void caseReentrantFromCallback()
{
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    antares::test::LoopbackServer server{
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok"};

    std::atomic<bool> done{false};
    std::atomic<bool> reentrant{false};

    (void)stack.client().submit(get(server.url() + "/"), [&](auto) {
        auto nested = stack.blocking().send(get(server.url() + "/nested"));
        reentrant.store(!nested && nested.error() == HttpError::reentrant);
        done.store(true);
    });

    for (int spin = 0; spin < 800 && !done.load(); ++spin) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(done.load(), "callback ran");
    check(reentrant.load(), "sync call from the loop thread reports reentrant");
}

void casePoolReusesConnection()
{
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    antares::test::LoopbackServer server{
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx"};

    (void)stack.blocking().send(get(server.url() + "/one"));
    (void)stack.blocking().send(get(server.url() + "/two"));

    const auto stats = stack.client().stats();
    check(stats.completed == 2, "both completed");
    check(stats.reused >= 1, "second request reused the connection");
}

struct Case {
    const char *name;
    void (*run)();
};

std::span<const Case> cases()
{
    static constexpr Case table[] = {
        {"async-get", caseAsyncGet},
        {"sync-get", caseSyncGet},
        {"redirect-followed", caseRedirectFollowed},
        {"reentrant-from-callback", caseReentrantFromCallback},
        {"pool-reuses-connection", casePoolReusesConnection},
    };
    return table;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--list") == 0) {
        for (const Case &item : cases()) {
            std::printf("%s\n", item.name);
        }
        return 0;
    }

    if (argc > 1) {
        for (const Case &item : cases()) {
            if (std::strcmp(argv[1], item.name) == 0) {
                item.run();
                std::printf("%s: %d check(s), %d failure(s)\n", item.name, checks, failures);
                return failures == 0 ? 0 : 1;
            }
        }
        std::fprintf(stderr, "unknown case: %s\n", argv[1]);
        return 2;
    }

    for (const Case &item : cases()) {
        item.run();
    }
    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
