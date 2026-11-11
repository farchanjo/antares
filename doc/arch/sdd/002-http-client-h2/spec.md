---
id: 01a1c1c1-03fe-7d91-838d-722d2326419b
number: 002
slug: http-client-h2
status: implemented
created_at: 2026-11-09T19:20:14.590125017Z
---
# Feature Specification: Http Client H2

Feature: 002-http-client-h2
Created: 2026-11-09

## User Stories

- As a Kubernetes desktop user I want every API call to run over HTTP/2 with TLS
  so that the cluster talks to the same protocol `kubectl` uses.
- As a caller I want an async API so that a hundred watches do not need a
  hundred threads.
- As a caller I want a sync API for one-shot requests so that a simple `GET`
  does not force a callback style.
- As a developer I want a spinning `kubectl get` to never freeze the UI, so the
  blocking variant must not be able to stall the engine.

## Functional Requirements

One `### FR-NNN` heading per requirement, numbered from `FR-001`: the id is what
a scenario in `doc/arch/specs/features/<slug>.feature` names to prove it, so a
requirement without one is a claim nothing exercises.

### FR-001 Transport runs on the engine

Every socket and TLS operation is submitted through `IoEngine` (P-09). The client
owns no thread of its own; the engine thread performs all I/O and all protocol
work. `QNetworkAccessManager` is never used.

### FR-002 The engine thread never blocks

No call on the engine thread may wait on a mutex the engine itself needs, sleep,
or perform a synchronous syscall. Concretely: a completion callback may call
`submit*` and may start another request, and both return without waiting.

Decoding is the one operation heavy enough to break this, so it does not run on
the loop: a chunk is handed to a parse worker and the loop moves on (FR-018).

### FR-003 Connection establishment

TCP connect, TLS handshake and protocol selection are three engine operations
chained by completions. A connect timeout, a TLS failure and an ALPN mismatch are
distinct errors, and each releases the connection slot.

### FR-004 HTTP/2

The client speaks HTTP/2 with ALPN `h2`: a connection preface, HPACK-encoded
headers, multiple concurrent streams per connection, and per-stream and
per-connection flow control. A server pushing above the window is respected, not
overrun.

### FR-005 HTTP/1.1 fallback

When ALPN negotiates `http/1.1` the same request API works over HTTP/1.1 with
`Connection: keep-alive`. The caller does not select the version by hand; a
per-request override exists for tests.

### FR-006 Redirects

`301`, `302`, `303`, `307` and `308` are followed, up to a configurable maximum
(default 10). `303` becomes `GET` and drops the body; `307` and `308` keep both.
A cross-origin hop drops `Authorization` unless the caller opts in. Exceeding the
maximum is an error naming the last location.

### FR-007 Async API

A request returns a handle immediately. Its completion runs on the engine thread
with a result or a typed error, exactly once. A cancelled request completes with
`cancelled` and never again.

### FR-008 No sync facade; callers await

There is no blocking request call. A caller submits and waits on its own
condition variable, never on engine state, so the wait is safe off the loop. A
call made from inside a completion returns `reentrant` instead of deadlocking —
that is a property of the caller's own helper, not of the client.

The `BlockingClient` facade and `HttpError::reentrant` are removed. Tests use the
`Await` helper, which already waits on its own mutex.

### FR-026 Shutdown drains, and the drain pumps the loop

A completion captures the client, so the client may not die with an attempt in
flight. `drain()` refuses new work, hands every idle connection back to the loop,
and **runs the loop** until nothing is pending — it cannot merely wait, because
once `app.exec()` has returned nothing else is processing the loop and a passive
wait never ends.

`drain()` takes a deadline, **5 seconds** by default — the same bound the test
`Await` uses, so a hang shows up in a test before it shows up at quit. On expiry
it logs `warn` with the count still pending, closes what it can and returns; a
hung peer may not hang the quit.

The bound is a `Limits` field (`shutdownDrain`), not a literal, so a slow profile
can raise it in one place.

The destructor asserts nothing is pending instead of guessing.

### FR-027 Two shutdown sources, one path

The process stops on a window close and on `SIGTERM`, and both reach the same
`drain()`.

A signal handler may only run async-signal-safe code, so it does one thing:
`write()` a byte to a self-pipe. A `QSocketNotifier` on that pipe, on the main
thread, calls `QCoreApplication::quit()`.

`QCoreApplication::aboutToQuit` runs **inside** the event loop and therefore
cannot wait either; it only marks the client closed, so a request submitted by a
closing widget is refused rather than started. `drain()` runs after `exec()`
returns.

The order is fixed: signal or close → `quit()` → `exec()` returns → `drain()` →
destruction.

### FR-009 TLS verification

Certificates are verified against the system trust store, the hostname is
checked against the certificate, and SNI is sent. A verification failure is a
typed error carrying the OpenSSL reason, never a silent success.

### FR-010 Bounded memory

Concurrent streams per connection, header list size, redirect hops and buffered
body bytes all have configured ceilings. Crossing one is a typed error, not an
unbounded allocation.

### FR-011 Pool configuration

Idle connections are pooled per authority with a per-host ceiling, a global
ceiling and an idle timeout. Crossing a ceiling closes the least recently used
idle connection; it never refuses a request.

### FR-012 Timeouts

Connect, read, write and whole-request deadlines are configurable, and zero
disables one. A read or write deadline resets on progress. A deadline that
expires is a typed `timeout`, never a silent retry. The engine grows a timer
facility so a deadline costs no thread.

### FR-013 Authentication

The client carries bearer tokens, basic credentials and client certificates for
mutual TLS. The credential is applied at header-build time and never stored in a
log record. Basic auth is base64 of `user:password` computed once, at
configuration load.

### FR-019 ALPN decides, h1 is the floor

TLS offers `h2` first and `http/1.1` second. The peer decides; a server without
h2 answers on 1.1 without a failed handshake. Plaintext stays on 1.1.

TLS 1.2 is the floor and the RFC 9113 Appendix A suites are never offered.

### FR-020 Multiplexing

Streams on one h2 connection are independent: a slow response never blocks a
fast one. The peer's `MAX_CONCURRENT_STREAMS` is the ceiling, and a request
above it waits rather than opening a second connection.

A GOAWAY stops new streams; the streams above its last id were never processed
and are retried on a fresh connection.

### FR-022 Chunks are delivered, not buffered

`submitStreaming` hands the head once and every body chunk as it decodes, on
both protocols: h1 by Content-Length or chunked framing, h2 per DATA frame. The
sink runs on the loop thread and must not block.

The read timeout is idle, not total: every chunk re-arms it, so a slow
`text/event-stream` survives while a silent one still times out.

### FR-021 Streaming, SSE and WebSocket

The data handler delivers bytes as they decrypt, so a response can be consumed
incrementally instead of buffered (streaming and `text/event-stream`).

WebSocket needs the 101 upgrade and then raw framing over the same connection;
the handler already exposes the byte pipe, so it is a layer on top, not a
transport change.

### FR-023 Event streams and sockets

`text/event-stream` is parsed by `SseParser`: bytes in, whole events out, with
the `Last-Event-ID` of the last one for reconnection.

WebSocket framing is `WsCodec` (RFC 6455 §5): client masks, control payloads
stay at 125 bytes or less, and a close frame closes the codec.

Both are pure domain types over the chunked transport, so neither blocks and
neither owns I/O.

### FR-025 Every port method is exercised

A virtual on a port has a test that reaches it. A counter with no increment and
a sink with no call site are defects, not future work: streaming survives a
redirect and a retry, and the client logs through the port.

### FR-024 The upgrade is the transport change

A socket is opened by sending the handshake GET and requiring a 101; the head is
consumed before any frame reaches the codec, because it shares a read with the
first frame. h1 only — over h2 this needs RFC 8441 extended CONNECT.

### FR-018 One loop, and the parallelism is elsewhere

The engine runs one loop on one thread. Connection state, the idle pool and the
counters are therefore single-threaded by construction and take no mutex. A
caller may submit from any thread; the work is posted onto the loop and every
step after that runs there.

The parallelism is not in the engine. It is a pool of parse workers outside it:
a loop takes a chunk, hands it out, and applies the result when it comes back
(FR-002 keeps the loop from waiting on it). This supersedes the earlier "fixed
set of loops": with one loop the three shared members that were not protected —
`sessions_`, `idle_` and `stats_` — stop being shared at all.

### FR-016 Resolution stays off the hot path

A literal address is parsed in-process, so it never blocks the loop. Only a host
name reaches `getaddrinfo`, and only on the loop thread. A name that must not
block at all is the open item: the resolver moves behind the port (FR-017).

### FR-017 No test sleeps on a clock

A test waits on a completion, never on a wall-clock delay. A failing wait is a
timeout on the waiter, never a spin.

### FR-014 One published configuration surface

The surface is declared in `doc/arch/schemas/http-client-config.cue`; the
retry, pool and timeout types live beside it and are referenced by name.

`ClientConfig` carries pool, timeouts, auth, limits and the two redirect and
verification switches. The client takes it by value at construction, exposes it
back through `config()`, and reads no environment variable of its own.

### FR-015 Retry with backoff

A failed attempt is retried according to a policy: total attempts, base delay,
multiplier, ceiling and jitter. Only idempotent methods retry by default, so a
`POST` is not silently duplicated. `Retry-After` wins over the computed delay
when the caller keeps that honour. Exhausting the attempts returns the last
response, not a synthetic error — the server did answer.

Retries wait on an engine timer, so the delay costs no thread (FR-012).

## Security Requirements

- **Data sensitivity/classification.** Requests carry cluster API payloads and
  `Authorization: Bearer` tokens. Bodies are treated as sensitive; the token is
  never logged.
- **Authentication/authorization.** The client transports an opaque credential
  it does not mint. It must not forward that credential across an origin change
  without an explicit opt-in (FR-006).
- **Input validation.** Status line, header names and values, `Content-Length`
  and chunk sizes are all untrusted. Header count, header bytes and body bytes
  are capped (FR-010); a malformed frame is a connection error, not a crash.
- **Cryptography in transit/at rest.** Every connection is TLS 1.2 or newer, with
  the system trust store as the only default trust anchor. Plaintext is opt-in
  per request, never silent.
- **Logging/audit.** Log lines carry method, host, status, stream id and
  duration at `debug`; `Authorization`, cookies and certificate bodies are
  redacted. Bodies appear only at `trace` and only truncated.
- **Error-handling information exposure.** Errors name the failure class and the
  peer, never a memory address or a raw buffer. A TLS error names the OpenSSL
  reason string only.

## Acceptance Scenarios

Given a request to a host with a valid certificate
When  the connection completes
Then  the response status and headers are delivered to the caller.

Given a `303` response with a `Location` header
When  the client follows it
Then  the follow-up is a `GET` with no body.

Given an async request whose handle is cancelled
When  the completion fires
Then  it carries `cancelled` and the slot is released.

Given a sync call made from inside a completion callback
When  it waits
Then  it returns without deadlocking the engine.

## Observability

Counters: connections opened, connections reused, streams opened, requests
completed by status class, redirects followed, TLS failures. Labels stay bounded
— scheme and status class only, never the host.

Log events at `debug`: connection established, stream opened, redirect followed,
connection closed with its reason. At `warn`: pool at cap, redirect limit hit. At
`error`: TLS verification failure.

Trace spans: one `http.request` span per attempt carrying method, status and
duration; a redirect creates a child span per hop. Conventions live in
`doc/arch/observability/observability.md`.
