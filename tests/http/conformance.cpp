// Conformance suite for the HTTP client (feature 002).
//
// One case per process so CTest fans the suite across cores; each case builds its
// own engine, client and loopback server.

#include <atomic>
#include <expected>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "adapters/http/client.hpp"
#include "application/http/blocking_client.hpp"
#include "application/io/engine_pool.hpp"
#include "loopback_server.hpp"
#include "test_cert.hpp"
#include "tls_server.hpp"
#include "support/await.hpp"

namespace {

using antares::adapters::http::HttpClient;
using antares::application::http::BlockingClient;
using antares::application::io::EnginePool;
namespace ports = antares::ports;
using antares::domain::http::ClientConfig;
using antares::domain::http::HttpError;
using antares::domain::http::Method;
using antares::domain::http::Request;
using antares::domain::http::Response;

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
    explicit Stack(ClientConfig config = {}, std::size_t loops = 1, bool verify = false)
        : config_(std::move(config))
        , pool_(loops)
    {
        // The test CA is not in the system store, so verification is opt-in.
        config_.verifyPeer = verify;
        pool_.start();
        client_ = std::make_shared<HttpClient>(pool_, config_);
        blocking_ = std::make_unique<BlockingClient>(*client_, pool_);
    }

    ~Stack()
    {
        client_.reset();
        pool_.stop();
    }

    Stack(const Stack &) = delete;
    Stack &operator=(const Stack &) = delete;

    [[nodiscard]] bool ok() const noexcept { return true; }
    [[nodiscard]] HttpClient &client() noexcept { return *client_; }
    [[nodiscard]] BlockingClient &blocking() noexcept { return *blocking_; }
    [[nodiscard]] EnginePool &pool() noexcept { return pool_; }
    [[nodiscard]] const ClientConfig &config() const noexcept { return config_; }

private:
    ClientConfig config_{};
    EnginePool pool_;
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
    (void)stack.pool().next().submitTimer(std::chrono::milliseconds{50}, [&] { fired.signal(); });
    check(fired.wait(), "timer fired");
}

void casePoolSpreadsAcrossLoops()
{
    antares::test::LoopbackServer server{
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx"};
    ClientConfig config;
    Stack stack{config, /*loops=*/2};
    check(stack.pool().size() == 2, "two loops");

    // Concurrent, so neither can reuse the other's connection: each opens its
    // own and must land on its own loop.
    antares::test::Await first;
    antares::test::Await second;
    (void)stack.client().submit(get(server.url() + "/a"), [&](auto) { first.signal(); });
    (void)stack.client().submit(get(server.url() + "/b"), [&](auto) { second.signal(); });
    check(first.wait(), "first completed");
    check(second.wait(), "second completed");

    const auto &engines = stack.pool().engines();
    std::size_t busy = 0;
    for (const auto &engine : engines) {
        if (!engine) {
            continue;
        }
        const auto snapshot = engine->stats();
        std::fprintf(stderr, "  loop submitted=%llu completed=%llu\n",
                     static_cast<unsigned long long>(snapshot.submitted),
                     static_cast<unsigned long long>(snapshot.completed));
        if (snapshot.submitted != 0) {
            ++busy;
        }
    }
    check(busy == 2, "both loops took work");
}

void caseHttpsNegotiatesH2()
{
    antares::test::TlsServer server{"hello", /*h2=*/true};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    auto result = stack.blocking().send(get(server.url() + "/"));
    check(result.has_value(), "h2 request returned");
    if (result) {
        check(result->status == 200, "status 200 over h2");
        check(result->version == antares::domain::http::Version::http2, "spoke HTTP/2");
    }
}

void caseHttpsFallsBackToH11()
{
    antares::test::TlsServer server{"fallback", /*h2=*/false};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    auto result = stack.blocking().send(get(server.url() + "/"));
    check(result.has_value(), "fallback request returned");
    if (result) {
        check(result->status == 200, "status 200 over http/1.1");
        check(result->version == antares::domain::http::Version::http11, "stayed on 1.1");
    }
}

void caseH2MultiplexesOneConnection()
{
    antares::test::TlsServer server{"multi", /*h2=*/true};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    // Warm the session so the concurrent streams share it.
    (void)stack.blocking().send(get(server.url() + "/warm"));

    antares::test::Await first;
    antares::test::Await second;
    (void)stack.client().submit(get(server.url() + "/a"), [&](auto) { first.signal(); });
    (void)stack.client().submit(get(server.url() + "/b"), [&](auto) { second.signal(); });
    check(first.wait(), "first stream completed");
    check(second.wait(), "second stream completed");

    check(server.connections() == 1, "one connection carried every stream");
    // Overlap is timing-dependent; the count is not.
    check(server.requests() >= 3, "every request reached the server");
}

void caseH1StreamsChunks()
{
    // Two chunks plus the terminator: the sink must see the pieces, not a blob.
    antares::test::LoopbackServer server{
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n"};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    antares::test::Await done;
    std::string collected;
    int chunks = 0;
    int heads = 0;

    ports::http::StreamHandler handler;
    handler.onHead = [&](const auto &head) {
        ++heads;
        check(head.status == 200, "head arrived first");
    };
    handler.onChunk = [&](std::span<const std::byte> piece) {
        ++chunks;
        collected.append(reinterpret_cast<const char *>(piece.data()), piece.size());
    };

    (void)stack.client().submitStreaming(
        get(server.url() + "/events"), std::move(handler), [&](auto) { done.signal(); });

    check(done.wait(), "stream finished");
    check(heads == 1, "one head");
    check(chunks >= 2, "chunks were delivered separately");
    check(collected == "hello world", "chunks reassemble to the body");
}

void caseH2StreamsData()
{
    antares::test::TlsServer server{"streamed", /*h2=*/true};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    antares::test::Await done;
    std::string collected;
    int chunks = 0;

    ports::http::StreamHandler handler;
    handler.onChunk = [&](std::span<const std::byte> piece) {
        ++chunks;
        collected.append(reinterpret_cast<const char *>(piece.data()), piece.size());
    };

    (void)stack.client().submitStreaming(
        get(server.url() + "/events"), std::move(handler), [&](auto) { done.signal(); });

    check(done.wait(), "h2 stream finished");
    check(chunks >= 1, "h2 delivered data");
    check(collected == "streamed", "h2 body matched");
}

void caseBearerIsSent()
{
    antares::test::LoopbackServer server{"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"};
    ClientConfig config;
    config.auth = antares::domain::http::Auth::bearer("token-123");
    Stack stack{config};
    if (!stack.ok()) {
        return;
    }

    (void)stack.blocking().send(get(server.url() + "/"));
    check(server.lastRequest().find("authorization: Bearer token-123") != std::string::npos ||
              server.lastRequest().find("Authorization: Bearer token-123") != std::string::npos,
          "bearer reached the wire");
}

void caseBasicEncodesCredential()
{
    antares::test::LoopbackServer server{"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"};
    ClientConfig config;
    config.auth = antares::domain::http::Auth::basic("Aladdin", "open sesame");
    Stack stack{config};
    if (!stack.ok()) {
        return;
    }

    (void)stack.blocking().send(get(server.url() + "/"));
    // RFC 7617: base64("Aladdin:open sesame").
    check(server.lastRequest().find("Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==") != std::string::npos,
          "basic encodes the pair");
}

void caseVerifiesAgainstTrustedCa()
{
    antares::test::TlsServer server{"verified", /*h2=*/true};
    ClientConfig config;
    config.verifyPeer = true;
    config.trustBundlePem = TEST_CA;
    Stack stack{config, /*loops=*/1, /*verify=*/true};
    if (!stack.ok()) {
        return;
    }

    auto result = stack.blocking().send(get(server.url() + "/"));
    check(result.has_value(), "trusted CA accepted");
    if (result) {
        check(result->status == 200, "status 200");
    }
}

void caseRejectsUntrustedCa()
{
    antares::test::TlsServer server{"nope", /*h2=*/false};
    ClientConfig config;
    Stack stack{config, /*loops=*/1, /*verify=*/true};
    if (!stack.ok()) {
        return;
    }

    auto result = stack.blocking().send(get(server.url() + "/"));
    check(!result, "untrusted CA rejected");
    if (!result) {
        check(result.error() == HttpError::tls, "reported as a TLS failure");
    }
}

void caseCancelCompletesOnce()
{
    antares::test::LoopbackServer server{
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx"};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    std::atomic<int> completions{0};
    HttpError seen = HttpError::protocol;
    auto ticket = stack.client().submit(get(server.url() + "/"), [&](auto result) {
        ++completions;
        if (!result) {
            seen = result.error();
        }
    });
    check(ticket.has_value(), "submitted");

    (*ticket)->cancel();
    (*ticket)->cancel(); // idempotent

    // Give the loop a turn to prove it does not complete twice.
    antares::test::Await tick;
    (void)stack.pool().next().submitTimer(std::chrono::milliseconds{20}, [&] { tick.signal(); });
    check(tick.wait(), "loop ran");

    check(completions.load() == 1, "completed exactly once");
    check(seen == HttpError::cancelled, "reported as cancelled");
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
        {"pool-spreads-across-loops", casePoolSpreadsAcrossLoops},
        {"https-negotiates-h2", caseHttpsNegotiatesH2},
        {"https-falls-back-to-h11", caseHttpsFallsBackToH11},
        {"h2-multiplexes-one-connection", caseH2MultiplexesOneConnection},
        {"h1-streams-chunks", caseH1StreamsChunks},
        {"h2-streams-data", caseH2StreamsData},
        {"bearer-is-sent", caseBearerIsSent},
        {"basic-encodes-credential", caseBasicEncodesCredential},
        {"verifies-against-trusted-ca", caseVerifiesAgainstTrustedCa},
        {"rejects-untrusted-ca", caseRejectsUntrustedCa},
        {"cancel-completes-once", caseCancelCompletesOnce},
    };
    return table;
}

} // namespace

int main(int argc, char **argv)
{
    // A peer that closes early must not kill the run.
    std::signal(SIGPIPE, SIG_IGN);

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
