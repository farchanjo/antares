# Implementation Plan: Kernel Io Engine Kqueue

## Overview

Build the `IoEngine` port and two backends: `kqueue` for Darwin production and
`poll` as the portable reference. Ship a conformance suite that drives both
through the port only. No HTTP, no TLS — the engine moves bytes.

## Technical Approach

**Layers.** `src/ports/io/` declares the port and `IoError`. Nothing there
includes Qt. `src/adapters/platform/io/` holds `kqueue_engine` and `poll_engine`.
`src/application/` owns the thread and the factory.

**Port surface.** Seven operations, five error cases, per ADR 0007. Operations
return `std::expected<void, IoError>`; callbacks are `std::move_only_function`
taking the operation result.

**Operation state.** One `PendingOp` per in-flight operation: descriptor,
buffer, callback, kind. The engine holds a stable-address store; a callback that
re-enters `submitRead` must not invalidate the running op.

**kqueue transform.** Register `EVFILT_READ`/`EVFILT_WRITE` with a pointer to
the `PendingOp` in `udata`. On `EV_EOF` map to `closed`; on `EV_ERROR` map to
`system`. Run the callback once, then unregister and release the op. `EV_ONESHOT`
carries the one-shot rule.

**Wakeup.** A self-pipe on the engine: `post()` writes one byte, the loop reads
it and drains the posted queue on the engine thread.

**poll backend.** Same `PendingOp` store, linear scan per sweep. Slower, and
deliberately so — it is the oracle, not the production path.

**Factory.** Compile-time selection by platform, runtime override by
`ANTARES_IO_BACKEND` for tests. `name()` reports the choice.

**Build.** New `antares_io` static library; `kqueue` sources guarded by
`if(APPLE)`. Conformance suite is a CTest target parameterised per backend.

**Test strategy.** The suite names no backend: it takes an `IoEngine&`. Each
case runs twice through two CTest instantiations. Fault injection for
`tooManyDescriptors` through a lowered descriptor limit.

## Constitution

- **P-04** — every file lands under `src/ports/io/` or
  `src/adapters/platform/io/`; nothing at the root.
- **P-06** — newest C++ only, no Qt type in the port; the engine compiles without
  the framework.
- **P-07** — the adapter depends on the port, never the reverse; the port
  includes no adapter header.
- **P-08** — the feature is developed and tested with the `dev` preset; the
  release configuration stays gated.
- **P-09** — the kernel is reached only through the port; no
  `QNetworkAccessManager` and no direct `kevent` above the adapter.
- **D-03** — includes point inward: adapter to port, port to standard library.
- **D-06** — includes are full paths from `src/`.
- **D-08** — no raw `new`; a failure returns `IoError`, never aborts.
- **D-17** — no new dependency; `kqueue` and `poll` are libc.

## Companion Artifacts

- `doc/arch/schemas/kernel-io-engine-kqueue.cue` — the closed error and
  operation sets this plan implements.

## Revision

The spec later gained FR-012 (idle costs nothing), FR-013 (the loop thread is
identified) and the resolver contract: `submitResolve` runs the lookup on a
thread and delivers the result on the loop, so no call blocks it.
