---
status: accepted
date: 2026-11-09
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0013 — HTTP/2 client on the engine, OpenSSL and nghttp2

## Context and Problem Statement

antares talks to the Kubernetes API server. That server is HTTP/2 with TLS, and
a watch keeps a stream open for hours. The client therefore has one hard
constraint before any other: it must never block the loop that carries its own
traffic.

Writing TLS and HPACK from scratch is possible but not free. HPACK alone is a
static-table plus Huffman codec with stateful invalidation; a bug there is a
desync that presents as a hang. The decision is what to implement and what to
link.

## Decision Drivers

- The engine loop is single-threaded and must stay unblocked (FR-002).
- macOS is the target (P-05); a Linux-only TLS stack is out.
- GPL-3.0-only project: licences must be compatible.
- `kubectl`-grade behaviour: h2, ALPN, redirect rules, verification.

## Considered Options

- **OpenSSL 3 + nghttp2, client logic ours** — both already on the host
  (OpenSSL 3.6.4, nghttp2), Apache-2.0 and MIT, portable.
- **Secure Transport + our HPACK** — native macOS, but deprecated and macOS-only.
- **Everything from scratch** — full control, weeks of surface, HPACK desync risk.
- **libcurl** — mature, but it wants its own event loop and would compete with
  the engine rather than sit on it.

## Decision Outcome

Chosen option: **OpenSSL 3 for TLS, nghttp2 for framing and HPACK, our client on
top through the engine.**

We write the parts that make it a client: connection lifecycle, pool, ALPN
selection, redirect policy, the sync and async facades, cancellation and
accounting. We do not write the record layer or the header codec.

**The engine thread never blocks.** The rules, in order of how easy they are to
get wrong:

- A completion callback runs on the engine thread. It may call `submit*` and it
  may start another request; both return immediately. It may not wait.
- The sync API waits on a per-request condition variable, never on engine
  internals, so no engine mutex is ever held across a wait.
- Reentrancy is detected: a sync call made from inside a callback returns
  `reentrant` instead of deadlocking (FR-008).
- TLS and h2 state belong to the connection, and only the engine thread touches
  it. No lock guards it, because there is nothing to guard against.

### Consequences

- Good: no thread per connection, so a hundred watches stay cheap.
- Good: both dependencies are portable and licence-compatible.
- Good: HPACK correctness is nghttp2's problem, not ours.
- Bad: two C libraries in the link, and OpenSSL 3 is a large surface.
- Bad: nghttp2 wants callbacks from its own API; the adapter between it and the
  engine is where a reentrancy bug would hide.
- Bad: HTTP/2 without server push support in the first cut.

### Confirmation

A request completes over h2 against a TLS peer; a sync call from inside a
callback returns `reentrant`; the loop's idle CPU stays near zero with a hundred
open streams.

## Threats Addressed

`doc/arch/threat-model/threat-model.md` carries the entry for each: TM-01 and
TM-02 (the peer is verified and a redirect cannot move the credential), TM-03
and TM-04 (the record layer and the size ceilings), TM-06 and TM-07 (the call
and the resume stay provable), TM-08, TM-09 and TM-10 (the credential, the pooled identity and the error
text), TM-11, TM-12 and TM-13 (idle sockets, stream concurrency and name
resolution).

## Superseded by ADR-0020

ADR-0020 removes the one-thread-per-engine rule: the engine is hosted in the Qt
main thread and stops owning a thread. Everything else in this record stands.
