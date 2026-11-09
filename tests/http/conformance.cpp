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
#include "support/await.hpp"

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
    explicit Stack(ClientConfig config = {})
        : config_(std::move(config))
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
    [[nodiscard]] const ClientConfig &config() const noexcept { return config_; }

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
    antares::test::LoopbackServer server{
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello"};
    Stack stack;
    check(stack.ok(), "engine built");
    if (!stack.ok()) {
        return;
    }

    antares::test::Await done;
    Response received;
    HttpError failure = HttpError::protocol;

    auto ticket = stack.client().submit(get(server.url() + "/"), [&](auto result) {
        if (result) {
            received = std::move(*result);
        } else {
            failure = result.error();
        }
        done.signal();
    });
    check(ticket.has_value(), "submit accepted");

    check(done.wait(), "completion fired");
    check(received.status == 200, "status 200");
    check(received.body.size() == 5, "body carried");
    check(failure == HttpError::protocol || received.status == 200, "no failure");
    check(server.lastRequest().rfind("GET / HTTP/1.1", 0) == 0, "request line");
}

void caseSyncGet()
{
    antares::test::LoopbackServer server{
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok"};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    auto result = stack.blocking().send(get(server.url() + "/sync"));
    check(result.has_value(), "sync call returned a response");
    if (result) {
        check(result->status == 200, "status 200");
        check(result->body.size() == 2, "body carried");
    }
}

void caseRedirectFollowed()
{
    antares::test::LoopbackServer server{std::vector<std::string>{
        "HTTP/1.1 302 Found\r\nLocation: /final\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok"}};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    auto result = stack.blocking().send(get(server.url() + "/start"));
    check(result.has_value(), "redirect returned");
    if (result) {
        check(result->hops == 1, "one hop recorded");
        check(server.lastRequest().rfind("GET /final HTTP/1.1", 0) == 0, "location followed");
    }
}

void caseReentrantFromCallback()
{
    antares::test::LoopbackServer server{
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok"};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    antares::test::Await done;
    std::atomic<bool> reentrant{false};

    (void)stack.client().submit(get(server.url() + "/"), [&](auto) {
        auto nested = stack.blocking().send(get(server.url() + "/nested"));
        reentrant.store(!nested && nested.error() == HttpError::reentrant);
        done.signal();
    });

    check(done.wait(), "callback ran");
    check(reentrant.load(), "sync call from the loop thread reports reentrant");
}

void casePoolReusesConnection()
{
    antares::test::LoopbackServer server{
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx"};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    (void)stack.blocking().send(get(server.url() + "/one"));
    (void)stack.blocking().send(get(server.url() + "/two"));

    const auto stats = stack.client().stats();
    check(stats.completed == 2, "both completed");
    check(stats.reused >= 1, "second request reused the connection");
}

void caseRetriesThenSucceeds()
{
    antares::test::LoopbackServer server{std::vector<std::string>{
        "HTTP/1.1 503 Busy\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 503 Busy\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok"}};
    ClientConfig config;
    config.retry.baseDelay = std::chrono::milliseconds{5};
    config.retry.jitter = 0.0;
    Stack stack{config};
    if (!stack.ok()) {
        return;
    }
    auto result = stack.blocking().send(get(server.url() + "/flaky"));
    check(result.has_value(), "eventually succeeded");
    if (result) {
        check(result->status == 200, "third attempt returned 200");
    }
    check(stack.client().stats().retries == 2, "two retries recorded");
}

void caseRetryStopsAtCeiling()
{
    antares::test::LoopbackServer server{
        "HTTP/1.1 503 Busy\r\nContent-Length: 0\r\n\r\n"};
    ClientConfig config;
    config.retry.maxAttempts = 2;
    config.retry.baseDelay = std::chrono::milliseconds{5};
    Stack stack{config};
    if (!stack.ok()) {
        return;
    }

    // Exhausting the attempts returns the last response, not an error: the
    // server did answer, it just answered 503.
    auto result = stack.blocking().send(get(server.url() + "/always-busy"));
    check(result.has_value(), "returned the final response");
    if (result) {
        check(result->status == 503, "last status surfaced");
    }
    check(stack.client().stats().retries == 1, "stopped at the ceiling");
}

void casePostNotRetried()
{
    antares::test::LoopbackServer server{
        "HTTP/1.1 503 Busy\r\nContent-Length: 0\r\n\r\n"};
    ClientConfig config;
    config.retry.baseDelay = std::chrono::milliseconds{5};
    Stack stack{config};
    if (!stack.ok()) {
        return;
    }

    Request request;
    request.method = Method::post;
    request.url = server.url() + "/submit";
    request.setBody("payload");

    (void)stack.blocking().send(std::move(request));
    check(stack.client().stats().retries == 0, "POST is not retried by default");
}

void caseIdleTimerFires()
{
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    // A timer is the only pending work: the loop must sleep, wake for the
    // deadline and run the task, not poll.
    antares::test::Await fired;
    (void)stack.engine().submitTimer(std::chrono::milliseconds{50}, [&] { fired.signal(); });
    check(fired.wait(), "timer fired");
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
        {"retries-then-succeeds", caseRetriesThenSucceeds},
        {"retry-stops-at-ceiling", caseRetryStopsAtCeiling},
        {"post-not-retried", casePostNotRetried},
        {"idle-timer-fires", caseIdleTimerFires},
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
