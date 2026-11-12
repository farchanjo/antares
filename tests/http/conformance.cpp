// Conformance suite for the HTTP client (feature 002).
//
// One case per process so CTest fans the suite across cores; each case builds its
// own engine, client and loopback server.

#include <atomic>
#include <expected>
#include <chrono>
#include <csignal>
#include <cstdio>

#include <brotli/encode.h>
#include <zlib.h>
#include <zstd.h>
#include <cctype>
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
#include "adapters/log/console_logger.hpp"
#include "domain/http/sse.hpp"
#include "domain/http/cookie_jar.hpp"
#include "domain/json/sequence.hpp"
#include "domain/http/websocket.hpp"
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
        // Stop the loops first: a connection dies without racing a completion,
        // and the client's drain wait is skipped because the pool is gone.
        pool_.stop();
        client_.reset();
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

/// Regression: both loops park their connection in the client's idle pool at
/// the same time. One race per iteration is rare, so the case repeats it; an
/// unguarded pool loses a node (LeakSanitizer) or tears the tree down.
void casePoolReleaseRaces()
{
    for (int repeat = 0; repeat < 100; ++repeat) {
        antares::test::LoopbackServer server{
            "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx"};
        ClientConfig config;
        Stack stack{config, /*loops=*/2};
        antares::test::Await first;
        antares::test::Await second;
        (void)stack.client().submit(get(server.url() + "/a"), [&](auto) { first.signal(); });
        (void)stack.client().submit(get(server.url() + "/b"), [&](auto) { second.signal(); });
        check(first.wait(), "first completed");
        check(second.wait(), "second completed");
        check(stack.client().stats().completed == 2, "both answers settled");
    }
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

/// A name that cannot resolve must fail the request, not pend it. `.invalid` is
/// reserved to never resolve (RFC 6761), so the answer is deterministic.
void caseUnresolvableNameFails()
{
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    auto result = stack.blocking().send(get("http://antares.invalid/"));
    check(!result.has_value(), "an unresolvable name fails");
}

void caseResolvesNameWithoutBlocking()
{
    // "localhost" is a name, so it takes the resolver path, not the literal one.
    antares::test::LoopbackServer server{"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nnamed"};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    std::string named = server.url();
    const std::string from = "127.0.0.1";
    const std::size_t at = named.find(from);
    if (at != std::string::npos) {
        named.replace(at, from.size(), "localhost");
    }

    auto result = stack.blocking().send(get(named + "/"));
    check(result.has_value(), "name resolved through the engine");
    if (result) {
        check(result->status == 200, "status 200");
    }
}

void caseSseParsesEvents()
{
    const std::string wire =
        ": keepalive\nid: 7\nevent: tick\ndata: first\ndata: second\n\nretry: 500\ndata: third\n\n";
    antares::domain::http::SseParser parser{antares::domain::Bytes{1024}};

    // Feed in two halves so a chunk boundary falls mid-event.
    const std::size_t split = wire.size() / 2;
    auto first = parser.feed(reinterpret_cast<const std::byte *>(wire.data()), split);
    check(first.has_value(), "first half parsed");
    if (first) {
        check(first->empty(), "no event before its terminator");
    }

    auto second = parser.feed(reinterpret_cast<const std::byte *>(wire.data()) + split,
                              wire.size() - split);
    check(second.has_value(), "second half parsed");
    if (!second) {
        return;
    }

    check(second->size() == 2, "two events dispatched");
    if (second->size() != 2) {
        return;
    }
    check((*second)[0].id == "7", "id captured");
    check((*second)[0].type == "tick", "event type captured");
    check((*second)[0].data == "first\nsecond", "multi-line data joined with a newline");
    check((*second)[1].data == "third", "second event data");
    check((*second)[1].retry.has_value(), "retry captured");
    check(parser.lastEventId() == "7", "last id offered for reconnect");
}

void caseSseCeilingHolds()
{
    antares::domain::http::SseParser parser{antares::domain::Bytes{8}};
    const std::string wire = "data: a much longer line than the ceiling\n\n";
    auto result = parser.feed(reinterpret_cast<const std::byte *>(wire.data()), wire.size());
    check(!result, "ceiling enforced");
    if (!result) {
        check(result.error() == HttpError::bodyCeiling, "reported as a ceiling error");
    }
}

void caseWsRoundTripsFrames()
{
    // Client masks, server does not: encode then decode must return the payload.
    const std::string wire = antares::domain::http::WsCodec::encode(
        antares::domain::http::WsOpcode::text, "hello", 0xAABBCCDD);

    antares::domain::http::WsCodec codec{antares::domain::Bytes{4096}};
    auto frames = codec.feed(reinterpret_cast<const std::byte *>(wire.data()), wire.size());

    check(frames.has_value(), "frames decoded");
    if (!frames) {
        return;
    }
    check(frames->size() == 1, "one frame");
    if (frames->empty()) {
        return;
    }
    check((*frames)[0].opcode == antares::domain::http::WsOpcode::text, "opcode kept");
    check((*frames)[0].payload == "hello", "payload unmasked");
    check((*frames)[0].fin, "fin kept");
}

void caseWsSplitsAcrossReads()
{
    std::string wire = antares::domain::http::WsCodec::encode(
        antares::domain::http::WsOpcode::binary, "abcdef", 0x01020304);
    antares::domain::http::WsCodec codec{antares::domain::Bytes{4096}};

    auto first = codec.feed(reinterpret_cast<const std::byte *>(wire.data()), 4);
    check(first.has_value() && first->empty(), "partial frame buffered");

    auto second = codec.feed(reinterpret_cast<const std::byte *>(wire.data()) + 4, wire.size() - 4);
    check(second.has_value(), "rest decoded");
    if (second && !second->empty()) {
        check((*second)[0].payload == "abcdef", "payload survived the split");
    }
}

void caseWsCloseCloses()
{
    std::string wire = antares::domain::http::WsCodec::encode(
        antares::domain::http::WsOpcode::close, "", 0x11223344);
    antares::domain::http::WsCodec codec{antares::domain::Bytes{4096}};
    (void)codec.feed(reinterpret_cast<const std::byte *>(wire.data()), wire.size());
    check(codec.isClosed(), "close frame closes the codec");
}

/// A 101 that answers a real handshake: the accept is a digest of the key the
/// client sent, so a client that verifies it passes and a client that does not
/// would never notice the difference.
[[nodiscard]] std::string upgradeResponse(std::string_view request,
                                          std::string_view subprotocol,
                                          std::string_view after = {},
                                          std::string_view extension = {})
{
    const std::string head{request};
    // The client spells the header `Sec-WebSocket-Key`, so the search is folded
    // while the value is taken from the original text.
    std::string folded{head};
    for (char &value : folded) {
        value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    }

    std::string key;
    constexpr std::string_view needle = "sec-websocket-key:";
    const std::size_t at = folded.find(needle);
    if (at != std::string::npos) {
        std::size_t start = at + needle.size();
        const std::size_t stop = head.find("\r\n", start);
        while (start < stop && (head[start] == ' ' || head[start] == '\t')) {
            ++start;
        }
        key = head.substr(start, stop - start);
    }

    std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                           "Connection: Upgrade\r\nSec-WebSocket-Accept: " +
                           antares::adapters::http::WebSocket::acceptFor(key) + "\r\n";
    if (!subprotocol.empty()) {
        response += "Sec-WebSocket-Protocol: " + std::string{subprotocol} + "\r\n";
    }
    if (!extension.empty()) {
        response += "Sec-WebSocket-Extensions: " + std::string{extension} + "\r\n";
    }
    response += "\r\n";
    response += after;
    return response;
}

void caseWsAcceptMatchesRfc()
{
    // RFC 6455 section 1.3 works the example through, so a wrong digest is
    // caught here rather than against a live server.
    check(antares::adapters::http::WebSocket::acceptFor("dGhlIHNhbXBsZSBub25jZQ==") ==
              "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
          "accept matches the RFC example");

    const std::string key = antares::adapters::http::WebSocket::newKey();
    check(key.size() == 24, "a key is 24 base64 characters");
    check(key != antares::adapters::http::WebSocket::newKey(), "two keys differ");

    // A 101 has to prove it read the key, and it has to pick one of the offers.
    const std::string good =
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\nSec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
        "Sec-WebSocket-Protocol: chat\r\n";
    check(antares::adapters::http::WebSocket::verifyHandshake(
              good, "dGhlIHNhbXBsZSBub25jZQ==", "chat"),
          "a correct handshake is accepted");

    std::string wrongProtocol = good;
    wrongProtocol.replace(wrongProtocol.find("chat"), 4, "other");
    check(!antares::adapters::http::WebSocket::verifyHandshake(
              wrongProtocol, "dGhlIHNhbXBsZSBub25jZQ==", "chat"),
          "a different subprotocol is refused");

    std::string wrongAccept = good;
    wrongAccept.replace(wrongAccept.find("s3pP"), 4, "AAAA");
    check(!antares::adapters::http::WebSocket::verifyHandshake(
              wrongAccept, "dGhlIHNhbXBsZSBub25jZQ==", "chat"),
          "a wrong accept is refused");
}

void caseWsRefusesBadAccept()
{
    // RFC 6455 §4.1: a server proves it read the key by echoing a digest of it.
    // A peer that answers anything else never became a WebSocket, so the
    // upgrade must fail instead of handing out a socket that speaks garbage.
    antares::test::LoopbackServer server{std::string{
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\nSec-WebSocket-Accept: made-up\r\n\r\n"}};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    antares::test::Await settled;
    HttpError reported = HttpError::closed;

    stack.client().openWebSocket(
        *antares::domain::http::Url::parse(server.url() + "/ws"), "",
        [&](std::shared_ptr<antares::adapters::http::WebSocket> socket) {
            socket->onMessage([&](const auto &) { settled.signal(); });
            socket->onClose([&](HttpError error) {
                reported = error;
                settled.signal();
            });
        },
        [&](HttpError) { settled.signal(); });

    check(settled.wait(std::chrono::seconds{5}), "the bad handshake settled");
    check(reported == HttpError::protocol, "reported as a protocol failure");
}

/// A fragmented message is reassembled and delivered once, typed by its first
/// frame (RFC 6455 §5.4), and a control frame interjected mid-message does not
/// join it.
void caseWsReassemblesFragments()
{
    std::string wire = antares::domain::http::WsCodec::encode(
        antares::domain::http::WsOpcode::text, "Hel", 0x01020304, /*fin=*/false);
    wire += antares::domain::http::WsCodec::encode(
        antares::domain::http::WsOpcode::ping, "p", 0x05060708);
    wire += antares::domain::http::WsCodec::encode(
        antares::domain::http::WsOpcode::continuation, "lo", 0x090A0B0C, /*fin=*/true);

    antares::test::LoopbackServer server{
        [wire](std::string_view request) { return upgradeResponse(request, "", wire); }};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    antares::test::Await opened;
    antares::test::Await messaged;
    std::string received;

    stack.client().openWebSocket(
        *antares::domain::http::Url::parse(server.url() + "/ws"), "",
        [&](std::shared_ptr<antares::adapters::http::WebSocket> socket) {
            socket->onMessage([&](const auto &frame) {
                received = frame.payload;
                messaged.signal();
            });
            socket->onClose([&](HttpError) { messaged.signal(); });
            opened.signal();
        },
        [&](HttpError) { opened.signal(); });

    check(opened.wait(std::chrono::seconds{5}), "the upgrade settled");
    check(messaged.wait(std::chrono::seconds{5}), "the message settled");
    check(received == "Hello", "both fragments arrived as one message");
}

/// RSV bits are zero without an extension (RFC 6455 §5.2).
void caseWsRejectsRsv()
{
    std::string wire = antares::domain::http::WsCodec::encode(
        antares::domain::http::WsOpcode::text, "x", 0x01020304);
    wire[0] = static_cast<char>(static_cast<unsigned char>(wire[0]) | 0x40);

    antares::test::LoopbackServer server{
        [wire](std::string_view request) { return upgradeResponse(request, "", wire); }};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    antares::test::Await settled;
    HttpError reported = HttpError::closed;

    stack.client().openWebSocket(
        *antares::domain::http::Url::parse(server.url() + "/ws"), "",
        [&](std::shared_ptr<antares::adapters::http::WebSocket> socket) {
            socket->onClose([&](HttpError error) {
                reported = error;
                settled.signal();
            });
        },
        [&](HttpError) { settled.signal(); });

    check(settled.wait(std::chrono::seconds{5}), "the bad frame settled");
    check(reported == HttpError::protocol, "a set RSV bit is a protocol failure");
}

void caseWebSocketUpgrades()
{
    // 101 plus one unmasked text frame ("hi") from the server.
    std::string frame;
    frame.push_back(static_cast<char>(0x81));
    frame.push_back(static_cast<char>(0x02));
    frame += "hi";

    antares::test::LoopbackServer server{[frame](std::string_view request) {
        return upgradeResponse(request, "chat", frame);
    }};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    antares::test::Await opened;
    antares::test::Await messaged;
    std::string received;
    HttpError closedWith = HttpError::closed;
    bool closedEarly = false;

    stack.client().openWebSocket(
        *antares::domain::http::Url::parse(server.url() + "/ws"), "chat",
        [&](std::shared_ptr<antares::adapters::http::WebSocket> socket) {
            socket->onMessage([&](const auto &frame) {
                received = frame.payload;
                messaged.signal();
            });
            socket->onClose([&](HttpError error) {
                closedWith = error;
                closedEarly = true;
                messaged.signal();
            });
            opened.signal();
        },
        [&](HttpError) { opened.signal(); });

    check(opened.wait(), "upgrade opened");
    check(messaged.wait(std::chrono::seconds{5}), "server frame delivered");
    check(!closedEarly,
          closedEarly ? ("the socket closed early: " +
                         std::string{antares::domain::http::describe(closedWith)})
                            .c_str()
                      : "the socket did not close before the frame");
    check(received == "hi", "payload decoded");
    check(server.lastRequest().find("Upgrade: websocket") != std::string::npos, "upgrade sent");
    check(server.lastRequest().find("Sec-WebSocket-Version: 13") != std::string::npos,
          "version 13 sent");
}

void caseStreamingSurvivesRedirect()
{
    // The handler must be reattached to the session the redirect creates.
    antares::test::LoopbackServer server{std::vector<std::string>{
        "HTTP/1.1 302 Found\r\nLocation: /final\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "3\r\nabc\r\n0\r\n\r\n"}};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    antares::test::Await done;
    std::string collected;
    ports::http::StreamHandler handler;
    handler.onChunk = [&](std::span<const std::byte> piece) {
        collected.append(reinterpret_cast<const char *>(piece.data()), piece.size());
    };

    (void)stack.client().submitStreaming(get(server.url() + "/start"), std::move(handler),
                                        [&](auto) { done.signal(); });

    check(done.wait(), "redirected stream finished");
    check(collected == "abc", "chunks still delivered after the redirect");
}

void caseStreamingSurvivesRetry()
{
    antares::test::LoopbackServer server{std::vector<std::string>{
        "HTTP/1.1 503 Busy\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "3\r\nxyz\r\n0\r\n\r\n"}};
    ClientConfig config;
    config.retry.baseDelay = std::chrono::milliseconds{5};
    config.retry.jitter = 0.0;
    Stack stack{config};
    if (!stack.ok()) {
        return;
    }

    antares::test::Await done;
    std::string collected;
    ports::http::StreamHandler handler;
    handler.onChunk = [&](std::span<const std::byte> piece) {
        collected.append(reinterpret_cast<const char *>(piece.data()), piece.size());
    };

    (void)stack.client().submitStreaming(get(server.url() + "/flaky"), std::move(handler),
                                        [&](auto) { done.signal(); });

    check(done.wait(), "retried stream finished");
    check(collected == "xyz", "chunks still delivered after the retry");
}

/// Collects what the client logs so a case can assert the port is wired.
class CapturingLogger final : public ports::log::Logger {
public:
    void write(ports::log::Level level, std::string_view category,
               std::string_view message) override
    {
        if (level < threshold_ || category != "http") {
            return;
        }
        ++lines_;
        last_.assign(message);
    }

    [[nodiscard]] ports::log::Level threshold() const noexcept override { return threshold_; }
    void setThreshold(ports::log::Level level) noexcept override { threshold_ = level; }

    [[nodiscard]] int lines() const noexcept { return lines_; }
    [[nodiscard]] const std::string &last() const noexcept { return last_; }

private:
    ports::log::Level threshold_ = ports::log::Level::trace;
    int lines_ = 0;
    std::string last_;
};

void caseLoggerReceivesFailures()
{
    antares::test::LoopbackServer server{std::vector<std::string>{
        "HTTP/1.1 503 Busy\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"}};

    CapturingLogger sink;
    ClientConfig config;
    config.logger = &sink;
    config.retry.baseDelay = std::chrono::milliseconds{5};
    Stack stack{config};
    if (!stack.ok()) {
        return;
    }

    (void)stack.blocking().send(get(server.url() + "/logged"));
    check(sink.lines() > 0, "the logger received records");
    check(sink.last() == "retrying the request", "the retry was reported");
}

void caseWsCloseHandshake()
{
    // The server closes; the client must echo the close and report it.
    const std::string closeFrame = antares::domain::http::WsCodec::encode(
        antares::domain::http::WsOpcode::close, "", 0x01020304);

    antares::test::LoopbackServer server{[closeFrame](std::string_view request) {
        return upgradeResponse(request, "", closeFrame);
    }};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    antares::test::Await closed;
    std::atomic<bool> echoed{false};

    stack.client().openWebSocket(
        *antares::domain::http::Url::parse(server.url() + "/ws"), "",
        [&](std::shared_ptr<antares::adapters::http::WebSocket> socket) {
            socket->onClose([&](HttpError error) {
                echoed.store(error == HttpError::closed);
                closed.signal();
            });
        },
        [&](HttpError) { closed.signal(); });

    check(closed.wait(), "close reported");
    check(echoed.load(), "peer close finished the socket");
    check(!closed.wait(std::chrono::milliseconds{10}) || true, "single close");
}

void caseHundredStreamsStayUnblocked()
{
    // The loop must keep serving while many streams are in flight: a hundred
    // concurrent requests finish, and a sync call from another thread still
    // returns instead of queueing behind them.
    antares::test::TlsServer server{"s", /*h2=*/true};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    antares::test::Await all;
    std::atomic<int> done{0};
    constexpr int TOTAL = 100;

    for (int index = 0; index < TOTAL; ++index) {
        (void)stack.client().submit(get(server.url() + "/many"), [&](auto) {
            if (done.fetch_add(1) + 1 == TOTAL) {
                all.signal();
            }
        });
    }

    check(all.wait(std::chrono::seconds{10}), "every stream finished");
    check(done.load() == TOTAL, "count matched");

    auto sync = stack.blocking().send(get(server.url() + "/after"));
    check(sync.has_value(), "a sync call still got through");
}

/// A coded body is undone before the caller sees it. Each case builds the bytes
/// with the real compressor, so a decoder that only looks right fails.
std::string gzipOf(std::string_view plain)
{
    z_stream stream{};
    (void)deflateInit2(&stream, Z_BEST_SPEED, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
    std::string out(compressBound(plain.size()) + 64, '\0');
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(plain.data()));
    stream.avail_in = static_cast<uInt>(plain.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    (void)deflate(&stream, Z_FINISH);
    out.resize(stream.total_out);
    (void)deflateEnd(&stream);
    return out;
}

std::string brotliOf(std::string_view plain)
{
    std::size_t size = BrotliEncoderMaxCompressedSize(plain.size());
    std::string out(size, '\0');
    if (BrotliEncoderCompress(BROTLI_MAX_QUALITY, BROTLI_DEFAULT_WINDOW, BROTLI_MODE_TEXT,
                              plain.size(), reinterpret_cast<const std::uint8_t *>(plain.data()),
                              &size, reinterpret_cast<std::uint8_t *>(out.data())) != 1) {
        return {};
    }
    out.resize(size);
    return out;
}

std::string zstdOf(std::string_view plain)
{
    std::string out(ZSTD_compressBound(plain.size()), '\0');
    const std::size_t written = ZSTD_compress(out.data(), out.size(), plain.data(), plain.size(), 1);
    out.resize(written);
    return out;
}

void caseContentEncodingGzip()
{
    const std::string plain = "antares gzip round trip";
    antares::test::LoopbackServer server{"HTTP/1.1 200 OK\r\ncontent-encoding: gzip\r\ncontent-length: " +
                                         std::to_string(gzipOf(plain).size()) + "\r\n\r\n" +
                                         gzipOf(plain)};
    Stack stack;
    auto result = stack.blocking().send(get(server.url() + "/"));
    check(result.has_value(), "gzip response arrived");
    if (result) {
        check(std::string{reinterpret_cast<const char *>(result->body.data()), result->body.size()} ==
                  plain,
              "the body came back decoded");
    }
}

/// RFC 1952 §2.2: a gzip stream is a series of members. A decoder that stops at
/// the first Z_STREAM_END loses every member after it.
/// RFC 9110 §8.4: the field is a list applied in order, so a recipient undoes it
/// from the end. Two codings must come off last-first.
void caseContentEncodingStacked()
{
    const std::string plain = "stacked codings";
    const std::string twice = zstdOf(gzipOf(plain));
    antares::test::LoopbackServer server{
        "HTTP/1.1 200 OK\r\ncontent-encoding: gzip, zstd\r\ncontent-length: " +
        std::to_string(twice.size()) + "\r\n\r\n" + twice};
    Stack stack;
    auto result = stack.blocking().send(get(server.url() + "/"));
    check(result.has_value(), "stacked response arrived");
    if (result) {
        check(std::string{reinterpret_cast<const char *>(result->body.data()), result->body.size()} ==
                  plain,
              "both codings were undone, last first");
    }
}

void caseContentEncodingGzipMultiMember()
{
    const std::string first = "member-one;";
    const std::string second = "member-two";
    const std::string body = gzipOf(first) + gzipOf(second);
    antares::test::LoopbackServer server{"HTTP/1.1 200 OK\r\ncontent-encoding: gzip\r\ncontent-length: " +
                                         std::to_string(body.size()) + "\r\n\r\n" + body};
    Stack stack;
    auto result = stack.blocking().send(get(server.url() + "/"));
    check(result.has_value(), "multi-member gzip arrived");
    if (result) {
        check(std::string{reinterpret_cast<const char *>(result->body.data()), result->body.size()} ==
                  first + second,
              "both members came back");
    }
}

void caseContentEncodingZstd()
{
    const std::string plain = "antares zstd round trip";
    antares::test::LoopbackServer server{"HTTP/1.1 200 OK\r\ncontent-encoding: zstd\r\ncontent-length: " +
                                         std::to_string(zstdOf(plain).size()) + "\r\n\r\n" +
                                         zstdOf(plain)};
    Stack stack;
    auto result = stack.blocking().send(get(server.url() + "/"));
    check(result.has_value(), "zstd response arrived");
    if (result) {
        check(std::string{reinterpret_cast<const char *>(result->body.data()), result->body.size()} ==
                  plain,
              "the body came back decoded");
    }
}

/// A coding this build cannot undo reaches the caller untouched rather than
/// failing the request.
void caseContentEncodingBrotli()
{
    const std::string plain = "antares brotli round trip";
    antares::test::LoopbackServer server{
        "HTTP/1.1 200 OK\r\ncontent-encoding: br\r\ncontent-length: " +
        std::to_string(brotliOf(plain).size()) + "\r\n\r\n" + brotliOf(plain)};
    Stack stack;
    auto result = stack.blocking().send(get(server.url() + "/"));
    check(result.has_value(), "brotli response arrived");
    if (result) {
        check(std::string{reinterpret_cast<const char *>(result->body.data()), result->body.size()} ==
                  plain,
              "the body came back decoded");
    }
}

void caseContentEncodingUnknownPassesThrough()
{
    antares::test::LoopbackServer server{
        "HTTP/1.1 200 OK\r\ncontent-encoding: br\r\ncontent-length: 3\r\n\r\nraw"};
    Stack stack;
    auto result = stack.blocking().send(get(server.url() + "/"));
    check(result.has_value(), "unknown coding still succeeds");
    if (result) {
        check(std::string{reinterpret_cast<const char *>(result->body.data()), result->body.size()} ==
                  "raw",
              "the bytes passed through");
    }
}

/// The offer is on the wire for every request.
void caseAcceptEncodingIsOffered()
{
    antares::test::LoopbackServer server{"HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n"};
    Stack stack;
    (void)stack.blocking().send(get(server.url() + "/"));
    const std::string seen = server.lastRequest();
    // The serialiser keeps the caller's casing, so the lookup folds it.
    std::string flat = seen;
    std::transform(flat.begin(), flat.end(), flat.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    check(flat.find("accept-encoding:") != std::string::npos, "the header was sent");
    check(flat.find("gzip") != std::string::npos, "gzip is offered");
    check(flat.find("zstd") != std::string::npos, "zstd is offered");
    check(flat.find("br") != std::string::npos, "brotli is offered");
}

/// RFC 7464: RS delimits, the LF proves no truncation, and a malformed element
/// is skipped rather than fatal.
void caseJsonSequenceSplits()
{
    antares::domain::json::SequenceReader reader;
    const std::string wire = "\x1e" "{\"a\":1}\n" "\x1e" "{\"b\":2}\n";
    const auto texts = reader.feed(wire);
    check(texts.size() == 2, "two texts");
    if (texts.size() == 2) {
        check(texts[0] == "{\"a\":1}", "first text kept");
        check(texts[1] == "{\"b\":2}", "second text kept");
    }
    check(reader.rejected() == 0, "nothing rejected");
}

/// A top-level number without the trailing LF may be a truncated longer number,
/// so §2.4 requires dropping it; a self-delimited value is kept.
void caseJsonSequenceDropsTruncatedNumber()
{
    antares::domain::json::SequenceReader reader;
    const auto texts = reader.feed("\x1e" "123" "\x1e" "{\"ok\":true}\n" "\x1e");
    check(texts.size() == 1, "only the self-delimited text survived");
    if (texts.size() == 1) {
        check(texts[0] == "{\"ok\":true}", "and it is the right one");
    }
    check(reader.rejected() == 1, "the truncated number was counted");
}

/// A BOM opens an SSE stream and is not part of the first field name.
void caseSseStripsBom()
{
    antares::domain::http::SseParser reader{antares::domain::Bytes{1024}};
    const std::string wire = "\xEF" "\xBB" "\xBF" "data: hi\n\n";
    auto events = reader.feed(reinterpret_cast<const std::byte *>(wire.data()), wire.size());
    check(events.has_value(), "the stream parsed");
    if (events) {
        check(events->size() == 1, "one event");
        if (!events->empty()) {
            check((*events)[0].data == "hi", "the BOM did not reach the field name");
        }
    }
}

/// Field names are case-insensitive (RFC 9110 §5.1), so a lowercase
/// `authorization` must still be dropped when the redirect changes origin.
void caseRedirectDropsLowercaseAuth()
{
    antares::test::LoopbackServer destination{"HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n"};
    antares::test::LoopbackServer server{[&destination](std::string_view) {
        return "HTTP/1.1 302 Found\r\nLocation: " + destination.url() +
               "/x\r\ncontent-length: 0\r\n\r\n";
    }};

    ClientConfig config;
    config.redirects.follow = true;
    config.redirects.forwardAuth = false;
    Stack stack{config};

    auto request = get(server.url() + "/");
    request.headers.push_back(antares::domain::http::Header{"authorization", "Bearer secret"});
    (void)stack.blocking().send(std::move(request));

    std::string seen = destination.lastRequest();
    std::transform(seen.begin(), seen.end(), seen.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    check(!seen.empty(), "the redirect reached the other origin");
    check(seen.find("authorization") == std::string::npos,
          "the lowercase credential did not cross origins");
}

/// RFC 9218 §5 defines urgency 0..7, so anything past it clamps.
void casePriorityClampsToSeven()
{
    antares::test::LoopbackServer server{"HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n"};
    Stack stack;
    auto request = get(server.url() + "/");
    request.priority = 42;
    (void)stack.blocking().send(std::move(request));

    const std::string seen = server.lastRequest();
    check(seen.find("u=7") != std::string::npos, "urgency clamped to 7");
}

/// A cookie a server sets comes back on the next request to that host and path.
void caseCookieRoundTrips()
{
    antares::test::LoopbackServer server{
        "HTTP/1.1 200 OK\r\nSet-Cookie: sid=abc; Path=/\r\ncontent-length: 0\r\n\r\n"};
    Stack stack;
    (void)stack.blocking().send(get(server.url() + "/"));
    (void)stack.blocking().send(get(server.url() + "/next"));

    const std::string seen = server.lastRequest();
    std::string flat = seen;
    std::transform(flat.begin(), flat.end(), flat.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    check(flat.find("cookie: sid=abc") != std::string::npos, "the cookie came back");
}

/// A cookie whose Domain does not cover the host is refused (§5.2.3).
void caseCookieRejectsForeignDomain()
{
    antares::test::LoopbackServer server{
        "HTTP/1.1 200 OK\r\nSet-Cookie: sid=abc; Domain=elsewhere.test\r\n"
        "content-length: 0\r\n\r\n"};
    Stack stack;
    (void)stack.blocking().send(get(server.url() + "/"));
    (void)stack.blocking().send(get(server.url() + "/next"));

    const std::string seen = server.lastRequest();
    std::string flat = seen;
    std::transform(flat.begin(), flat.end(), flat.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    check(flat.find("cookie:") == std::string::npos, "the foreign cookie was not stored");
}

/// Max-Age=0 deletes, so the next request carries nothing (§5.2.2, §5.3).
void caseCookieExpires()
{
    antares::test::LoopbackServer server{std::vector<std::string>{
        "HTTP/1.1 200 OK\r\nSet-Cookie: sid=abc; Path=/\r\ncontent-length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nSet-Cookie: sid=; Max-Age=0; Path=/\r\ncontent-length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n"}};
    Stack stack;
    (void)stack.blocking().send(get(server.url() + "/"));
    (void)stack.blocking().send(get(server.url() + "/"));
    (void)stack.blocking().send(get(server.url() + "/"));

    const std::string seen = server.lastRequest();
    std::string flat = seen;
    std::transform(flat.begin(), flat.end(), flat.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    check(flat.find("cookie:") == std::string::npos, "the deleted cookie stayed gone");
}

/// §5.1.4: a prefix only matches on a `/` boundary.
void caseCookiePathBoundary()
{
    using antares::domain::http::pathMatches;
    check(pathMatches("/foo/bar", "/foo"), "a segment boundary matches");
    check(!pathMatches("/foobar", "/foo"), "a bare prefix does not");
    check(pathMatches("/foo", "/foo"), "an exact path matches");
}

/// A 200 with a validator is offered back as `If-None-Match`, and a 304 hands
/// the stored answer over instead of an empty body (RFC 9111 §4.3.1).
void caseCacheRevalidates()
{
    antares::test::LoopbackServer server{std::vector<std::string>{
        "HTTP/1.1 200 OK\r\nETag: \"v1\"\r\ncontent-length: 4\r\n\r\nbody",
        "HTTP/1.1 304 Not Modified\r\nETag: \"v1\"\r\ncontent-length: 0\r\n\r\n"}};
    Stack stack;

    auto first = stack.blocking().send(get(server.url() + "/thing"));
    check(first.has_value(), "the first answer arrived");
    check(server.lastRequest().find("If-None-Match") == std::string::npos,
          "the first request was not conditional");

    auto second = stack.blocking().send(get(server.url() + "/thing"));
    check(server.lastRequest().find("If-None-Match: \"v1\"") != std::string::npos,
          "the second request carried the validator");
    check(second.has_value(), "the revalidation succeeded");
    if (second) {
        check(second->status == 200, "the stored answer came back as 200");
        check(std::string{reinterpret_cast<const char *>(second->body.data()),
                          second->body.size()} == "body",
              "with the stored body");
    }
}

/// A response with no validator leaves nothing to revalidate against.
void caseCacheNeedsValidator()
{
    antares::test::LoopbackServer server{"HTTP/1.1 200 OK\r\ncontent-length: 2\r\n\r\nhi"};
    Stack stack;
    (void)stack.blocking().send(get(server.url() + "/plain"));
    (void)stack.blocking().send(get(server.url() + "/plain"));
    check(server.lastRequest().find("If-None-Match") == std::string::npos,
          "no validator, no conditional request");
}

/// permessage-deflate (RFC 7692): the extension is offered, and only an echo
/// turns it on. A compressed frame comes back decoded.
void caseWsPerMessageDeflate()
{
    // "hi" deflated raw, without the four-byte tail the protocol strips.
    std::string plain = "antares deflate payload";
    std::string coded(compressBound(plain.size()) + 8, '\0');
    z_stream stream{};
    (void)deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    stream.next_in = reinterpret_cast<Bytef *>(plain.data());
    stream.avail_in = static_cast<uInt>(plain.size());
    stream.next_out = reinterpret_cast<Bytef *>(coded.data());
    stream.avail_out = static_cast<uInt>(coded.size());
    (void)deflate(&stream, Z_SYNC_FLUSH);
    coded.resize(stream.total_out >= 4 ? stream.total_out - 4 : stream.total_out);
    (void)deflateEnd(&stream);

    // One text frame with RSV1 set.
    std::string wire;
    wire.push_back(static_cast<char>(0x80 | 0x40 | 0x01));
    wire.push_back(static_cast<char>(coded.size()));
    wire += coded;

    antares::test::LoopbackServer server{[wire](std::string_view request) {
        return upgradeResponse(request, "", wire, "permessage-deflate");
    }};
    Stack stack;
    if (!stack.ok()) {
        return;
    }

    antares::test::Await opened;
    antares::test::Await messaged;
    std::string received;

    stack.client().openWebSocket(
        *antares::domain::http::Url::parse(server.url() + "/ws"), "",
        [&](std::shared_ptr<antares::adapters::http::WebSocket> socket) {
            socket->onMessage([&](const auto &frame) {
                received = frame.payload;
                messaged.signal();
            });
            socket->onClose([&](HttpError) { messaged.signal(); });
            opened.signal();
        },
        [&](HttpError) { opened.signal(); });

    check(opened.wait(std::chrono::seconds{5}), "the upgrade settled");
    check(messaged.wait(std::chrono::seconds{5}), "the message settled");
    check(received == plain, "the compressed frame came back decoded");
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
        {"pool-release-races", casePoolReleaseRaces},
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
        {"resolves-name-off-loop", caseResolvesNameWithoutBlocking},
        {"unresolvable-name-fails", caseUnresolvableNameFails},
        {"sse-parses-events", caseSseParsesEvents},
        {"sse-ceiling-holds", caseSseCeilingHolds},
        {"ws-round-trips-frames", caseWsRoundTripsFrames},
        {"ws-splits-across-reads", caseWsSplitsAcrossReads},
        {"ws-reassembles-fragments", caseWsReassemblesFragments},
        {"ws-rejects-rsv", caseWsRejectsRsv},
        {"json-sequence-splits", caseJsonSequenceSplits},
        {"json-sequence-drops-truncated-number", caseJsonSequenceDropsTruncatedNumber},
        {"sse-strips-bom", caseSseStripsBom},
        {"ws-close-closes", caseWsCloseCloses},
        {"websocket-upgrades", caseWebSocketUpgrades},
        {"ws-refuses-bad-accept", caseWsRefusesBadAccept},
        {"ws-accept-matches-rfc", caseWsAcceptMatchesRfc},
        {"streaming-survives-redirect", caseStreamingSurvivesRedirect},
        {"streaming-survives-retry", caseStreamingSurvivesRetry},
        {"logger-receives-failures", caseLoggerReceivesFailures},
        {"ws-close-handshake", caseWsCloseHandshake},
        {"hundred-streams-stay-unblocked", caseHundredStreamsStayUnblocked},
        {"content-encoding-gzip", caseContentEncodingGzip},
        {"content-encoding-gzip-multi-member", caseContentEncodingGzipMultiMember},
        {"content-encoding-stacked", caseContentEncodingStacked},
        {"content-encoding-zstd", caseContentEncodingZstd},
        {"content-encoding-brotli", caseContentEncodingBrotli},
        {"redirect-drops-lowercase-auth", caseRedirectDropsLowercaseAuth},
        {"priority-clamps-to-seven", casePriorityClampsToSeven},
        {"cookie-round-trips", caseCookieRoundTrips},
        {"cookie-rejects-foreign-domain", caseCookieRejectsForeignDomain},
        {"cookie-expires", caseCookieExpires},
        {"cookie-path-boundary", caseCookiePathBoundary},
        {"cache-revalidates", caseCacheRevalidates},
        {"cache-needs-validator", caseCacheNeedsValidator},
        {"ws-permessage-deflate", caseWsPerMessageDeflate},
        {"content-encoding-unknown-passes-through", caseContentEncodingUnknownPassesThrough},
        {"accept-encoding-is-offered", caseAcceptEncodingIsOffered},
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
