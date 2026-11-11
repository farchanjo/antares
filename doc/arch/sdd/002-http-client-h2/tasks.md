# Tasks: Http Client H2

Feature: 002-http-client-h2
Plan: `doc/arch/sdd/002-http-client-h2/plan.md`

Ordered so each task compiles on its own and proves one requirement.

- [x] T001 Add OpenSSL 3 and nghttp2 as pinned `find_package` dependencies and
      link them into a new `antares_http` target (D-17).
- [x] T002 Define the domain value types: request, response, limits, error,
      URL parse (FR-001, FR-010).
- [x] T003 Declare the async client port with `submit`, `cancel` and `stats`
      (FR-007).
- [x] T004 Build the TLS context: trust store, SNI, hostname verification and a
      typed verification error (FR-009).
- [x] T005 Implement the connection state machine connect → handshake → ALPN →
      ready, each stage one engine operation (FR-001, FR-003).
- [x] T006 Implement the h2 session over nghttp2 with per-stream state, flow
      control and a bounded send buffer (FR-004, FR-010).
- [x] T007 Implement the HTTP/1.1 keep-alive session and the ALPN selection that
      picks it (FR-005).
- [x] T008 Implement redirect policy: status mapping, method rewrite, credential
      drop on origin change, hop ceiling (FR-006).
- [x] T009 Implement the connection pool with idle timeout and a per-authority
      cap (FR-010).
- [x] T010 Implement the blocking facade with reentrancy detection (FR-008).
- [x] T011 Add a loopback TLS test server and prove h2, h1 fallback, redirect
      and cancellation on both engine backends (FR-003…FR-010).
- [x] T012 Prove the engine stays unblocked: a hundred open streams, sync call
      from a callback, idle CPU near zero (FR-002).
- [x] T013 Wire the counters and log events the spec's Observability section
      names (FR-007).

- [x] T014 Add a timer to the engine port so a deadline costs no thread
      (FR-012).
- [x] T015 Apply the configured credential when headers are built, covering
      bearer, basic and client certificate (FR-013).

- [x] T016 Add the engine timer, retry policy and backoff (FR-012, FR-015).

- [x] T017 Move name resolution behind the engine port so no call blocks the
      loop, with the numeric path kept as the fast route (FR-016).

- [x] T018 Give the engine a loop pool and key the connection pool by loop (FR-018).

- [x] T019 Negotiate ALPN, speak h2, multiplex and fall back to 1.1 (FR-019, FR-020).
- [x] T020 Streaming completion with an idle timeout (FR-022).
- [x] T021 SSE framing and the WebSocket codec (FR-023).
- [x] T022 Complete the 101 upgrade path so a socket is opened from a response.
- [x] T023 Answer pings and drive a close handshake (RFC 6455 §5.5).

- [x] T024 Wire the log port into the client and keep stream sinks across
      redirect and retry (FR-025).

## Dependencies

- T002 before T003; T004 and T005 before T006; T006 before T011.
- T007 is independent of T006 and can land in parallel.
- T012 runs only after T011 exists to drive it.

## Shutdown and facade removal

- [ ] T901 Add `drain()`: wait until nothing is pending, then hand every idle
      connection back to the loop; assert it is called off the loop (FR-026).
- [ ] T902 Make the destructor assert nothing is pending instead of skipping the
      wait when it runs on the loop (FR-026).
- [ ] T903 Remove `BlockingClient`, `HttpError::reentrant` and its `toString`
      case; move the twenty `blocking()` call sites to `Await` over `submit`
      (FR-008).
- [ ] T904 Add the case that `drain()` with an attempt in flight waits and closes
      the idle bucket (FR-026).
