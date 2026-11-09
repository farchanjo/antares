# Implementation Plan: Http Client H2

Feature: 002-http-client-h2
Spec: `doc/arch/sdd/002-http-client-h2/spec.md`
ADR: `doc/arch/adr/0013-http-client-h2.md`

## Approach

Two layers, one direction. Value types and the client port sit inward; the
OpenSSL and nghttp2 adapters sit outward and reach the network only through
`IoEngine`. The sync facade is application code over the async port, so it never
touches a socket.

The rule that shapes everything: **the engine thread never waits.** Protocol
state belongs to the connection and only the engine thread touches it, so no
mutex guards it. The sync facade waits on a per-request condition variable and
detects reentrancy by comparing thread ids with the engine thread.

## Layers and files

- `src/domain/http/request.hpp` — method, URL, headers, body, version override.
- `src/domain/http/response.hpp` — status, version, headers, body, hop count.
- `src/domain/http/limits.hpp` — the ceilings from FR-010, with defaults.
- `src/domain/http/http_error.hpp` — the closed error set.
- `src/domain/http/url.hpp` — parse into scheme, authority, target; the only
  place a URL is taken apart.
- `src/ports/http/client.hpp` — `submit`, `cancel`, `stats`; async only.
- `src/application/http/blocking_client.hpp` — sync facade over the port.
- `src/adapters/http/tls_context.*` — OpenSSL context, trust store, SNI.
- `src/adapters/http/connection.*` — one connection: TCP, TLS, ALPN, then h2 or
  h1 framing.
- `src/adapters/http/h2_session.*` — nghttp2 callbacks, per-stream state.
- `src/adapters/http/h1_session.*` — HTTP/1.1 keep-alive path.
- `src/adapters/http/redirect.*` — hop policy: method rewrite, credential drop.
- `src/adapters/http/pool.*` — idle reuse keyed by scheme and authority.
- `src/adapters/http/client.*` — the port implementation tying them together.

## Engine interaction

Each connection is a state machine advanced only by completions: connect →
handshake → write → read → close. Every transition submits one operation and
returns. nghttp2 produces bytes into a send buffer; the adapter drains it with a
single pending write. Incoming bytes feed `nghttp2_session_mem_recv`, which
yields frames and never blocks.

## Sync facade

`blocking_client::get()` takes a per-request mutex and condition variable,
submits, then waits. If the calling thread is the engine thread, waiting would
deadlock, so the facade reports `reentrant` (FR-008). The engine thread id is
published once at start and compared by value.

## Dependencies

- OpenSSL 3 — TLS, pinned by exact version from the Homebrew prefix.
- nghttp2 — framing and HPACK, pinned the same way.
- Both are `find_package` targets with an exact version requirement; the pin
  checker (D-17) reads the version from `CMakeLists.txt`.

## Testing

The conformance suite grows a loopback TLS server with a self-signed
certificate, so h2, h1 fallback, redirects and cancellation are proven without a
network. Each case runs on both the engine backends, and the reentrancy case
runs its sync call from inside a callback to prove the deadlock is gone.

## Constitution

The plan answers the doctrines that bind it:

- **D-01** — this plan follows `spec.md`; the FR list is the source of truth and
  the code cites it.
- **D-03** — the transport stays behind the `IoEngine` port; the domain keeps
  only types, with no I/O.
- **D-06** — every production include names its full path from `src/`.
- **D-08** — no bare `value()` or raw `new`; absence is `std::expected`.
- **P-05** — acceptance runs on macOS first.
- **P-09** — nothing reaches the kernel except through the engine port.
- **P-10** — every log call names a level and its source category.
- **P-11** — C++26 with `std::expected` and `std::span`, native chrono types.
- **P-12** — ownership is `unique_ptr`; `shared_ptr` only where a stream or a
  completion genuinely shares a lifetime.
- **P-13** — `asan` and `leak` gate every run.

## Revision

Later spec revisions added FR-015 to FR-025: retry with backoff, a loop pool,
chunked streaming with an idle timeout, SSE, WebSocket framing and upgrade,
ALPN with h1 fallback, header-table and priority handling, and port coverage.
Each landed with its own case; this plan stays the map, not the history.
