---
status: proposed
date: 2026-11-09
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0007 — IoEngine interface shape and kqueue backend

## Context and Problem Statement

ADR 0006 fixes the port, the completion semantics, buffer ownership and the
single-thread rule. It leaves the concrete interface open. An interface written
ad hoc becomes a leak: once a callback signature is public, every later backend
inherits it, and a mistake in the error taxonomy outlives the backend that
motivated it.

kqueue is the first backend and the only production one on macOS. It is also a
readiness primitive, so it must transform readiness into the completion
semantics the port promises.

## Decision Drivers

- The interface is the expensive artifact; a backend is replaceable.
- A conformance suite needs a second backend to be meaningful.
- `std::expected` keeps failure in the type system (D-08).
- One thread makes the transform tractable and the cancellation rule provable.

## Considered Options

- **`IoError` as `enum class`** — exhaustive, cheap, no allocation.
- **`IoError` as `std::error_code`** — standard, but open and stringly at the
  boundary.
- **Callback returning the error** — forces every caller to re-dispatch.
- **`poll` written only as a test double** — no second real backend.

## Decision Outcome

Chosen option: **`enum class IoError` returned in `std::expected`, with `poll`
as a real second backend.**

The port declares seven operations: `submitRead`, `submitWrite`,
`submitAccept`, `submitConnect`, `post`, `run`, `stop`. Each fallible operation
returns `std::expected<void, IoError>` and takes its completion callback by
value.

`IoError` has five cases: `wouldBlock`, `closed`, `cancelled`,
`tooManyDescriptors`, `system`. A case is added only with a scenario that
reaches it.

`poll` is not a test double. It is a compiled backend behind the same port, and
the conformance suite runs it beside `kqueue`. A suite that only ever sees one
backend proves the backend, not the port.

The kqueue backend converts readiness into completion with a per-operation state
machine: register `EVFILT_READ`/`EVFILT_WRITE`, hold the operation until the
filter fires, run the callback once, unregister.

### Consequences

- Good: the interface is reviewable before any backend depends on it.
- Good: `poll` keeps the suite honest on every OS.
- Bad: two backends to maintain before the first connection exists.
- Bad: `tooManyDescriptors` exists because kqueue can fail registration at the
  limit; `poll` never reaches it, so the suite needs an injected fault.
- Bad: five cases are a closed set that a real failure may not fit.

## Superseded by ADR-0020

ADR-0020 takes the loop away from the engine, so kqueue no longer owns a
thread. Its primitives and its event shapes stand.

### Confirmation

`src/ports/io/` declares exactly those seven operations and five error cases;
the conformance suite runs both backends and names neither.
