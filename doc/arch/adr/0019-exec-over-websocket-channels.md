---
status: accepted
date: 2026-11-09
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0019 — Exec over the WebSocket channel subprotocol

## Context and Problem Statement

Opening a shell in a pod needs a stream the server can push to and the client can
type into: bidirectional, ordered, and separable into stdin, stdout and stderr.
The API server offers this as an HTTP upgrade on the pod's `exec` subresource.

Feature 005 declared `exec` and stopped there. The transport was left open, and
the port has no `exec` at all, so nothing can be written until the wire shape is
pinned.

## Decision Drivers

- The three streams must not interleave. A stderr line rendered on stdout is a
  lie the user cannot detect.
- A resize is a control message, not payload.
- Failure has to arrive as a `Status`, the same document every other verb maps.
- The framing must survive a TTY (bytes, not text).

## Considered Options

- **`v5.channel.k8s.io`.** Binary frames, first byte selects the channel.
- **`v4.channel.k8s.io`.** Text frames, payload base64-encoded, same channels.
  Deprecated since 1.30.
- **SPDY.** The original transport. Removed from the API server in 1.31.

## Decision Outcome

Chosen option: **`v5.channel.k8s.io` only.**

One byte prefixes every frame and names its channel. The payload follows
verbatim: no base64, so a TTY sees the bytes the process wrote.

| Channel | Direction | Payload |
|---------|-----------|---------|
| 0 | client → server | stdin |
| 1 | server → client | stdout |
| 2 | server → client | stderr |
| 3 | server → client | a `Status` document, only on failure |
| 4 | client → server | resize, `{"Width":c,"Height":r}` |
| 5 | client → server | close |

Channel 3 is reported through its own callback, never merged into stdout or
stderr, and decodes to the same `ApiError` the other verbs produce.

A session is a `Ticket` like every other call: cancelling it closes the socket
once, and a second cancel is a no-op.

### Consequences

- Good: one code path. `v4` would add a base64 pair and a second test matrix.
- Good: binary end to end, so a TTY is not corrupted.
- Bad: a cluster older than 1.31 offers only `v4` and is refused. 1.31 shipped in
  August 2024 and is out of support, so the gap is narrow.
- Bad: the channel byte is a second framing on top of WebSocket, so a bug in one
  is easy to read as a bug in the other.

## Pros and Cons of the Options

### `v4.channel.k8s.io`

Buys 1.21 through 1.30, at the cost of base64 on both sides and a matrix that
must run twice. Reconsider only if a target cluster is that old.

### SPDY

Gone from the server. Keeps nothing alive.

## More Information

Related: ADR-0013 (the HTTP client), ADR-0017 (this client). The framing lives in
the exec port; `WsCodec` already produces and consumes the frames underneath.
