# Tasks: Http Client H2

Feature: 002-http-client-h2
Plan: `doc/arch/sdd/002-http-client-h2/plan.md`

Ordered so each task compiles on its own and proves one requirement.

- [ ] T001 Add OpenSSL 3 and nghttp2 as pinned `find_package` dependencies and
      link them into a new `antares_http` target (D-17).
- [ ] T002 Define the domain value types: request, response, limits, error,
      URL parse (FR-001, FR-010).
- [ ] T003 Declare the async client port with `submit`, `cancel` and `stats`
      (FR-007).
- [ ] T004 Build the TLS context: trust store, SNI, hostname verification and a
      typed verification error (FR-009).
- [ ] T005 Implement the connection state machine connect → handshake → ALPN →
      ready, each stage one engine operation (FR-001, FR-003).
- [ ] T006 Implement the h2 session over nghttp2 with per-stream state, flow
      control and a bounded send buffer (FR-004, FR-010).
- [ ] T007 Implement the HTTP/1.1 keep-alive session and the ALPN selection that
      picks it (FR-005).
- [ ] T008 Implement redirect policy: status mapping, method rewrite, credential
      drop on origin change, hop ceiling (FR-006).
- [ ] T009 Implement the connection pool with idle timeout and a per-authority
      cap (FR-010).
- [ ] T010 Implement the blocking facade with reentrancy detection (FR-008).
- [ ] T011 Add a loopback TLS test server and prove h2, h1 fallback, redirect
      and cancellation on both engine backends (FR-003…FR-010).
- [ ] T012 Prove the engine stays unblocked: a hundred open streams, sync call
      from a callback, idle CPU near zero (FR-002).
- [ ] T013 Wire the counters and log events the spec's Observability section
      names (FR-007).

## Dependencies

- T002 before T003; T004 and T005 before T006; T006 before T011.
- T007 is independent of T006 and can land in parallel.
- T012 runs only after T011 exists to drive it.
