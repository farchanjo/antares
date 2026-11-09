---
id: 01a1c1c1-03fe-7d91-838d-722d2326419b
number: 002
slug: http-client-h2
status: draft
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

### FR-008 Sync API

A blocking call exists for one-shot requests. It waits on the request's own
condition variable, never on engine state, so it is safe off the engine thread.
Called from inside a completion callback it detects reentrancy and returns an
error instead of deadlocking.

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

### FR-021 Streaming, SSE and WebSocket

The data handler delivers bytes as they decrypt, so a response can be consumed
incrementally instead of buffered (streaming and `text/event-stream`).

WebSocket needs the 101 upgrade and then raw framing over the same connection;
the handler already exposes the byte pipe, so it is a layer on top, not a
transport change.

### FR-018 A dedicated pool of loops

The engine owns a fixed set of loops and their threads; a caller takes one and
keeps it for the life of the work. Nothing is shared between loops, so no lock
guards connection state and the pool never touches an operation.

The pool is per loop as well: a pooled connection may only be reused from the
loop that owns it, or the attempt would run on one loop and its socket on
another.

### FR-016 Resolution stays off the hot path

A literal address is parsed in-process, so it never blocks the loop. Only a host
name reaches `getaddrinfo`, and only on the loop thread. A name that must not
block at all is the open item: the resolver moves behind the port (FR-017).

### FR-017 No test sleeps on a clock

A test waits on a completion, never on a wall-clock delay. A failing wait is a
timeout on the waiter, never a spin.

### FR-014 One published configuration surface

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
