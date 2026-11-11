---
status: accepted
date: 2026-11-09
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0006 — Kernel I/O engine behind a port

## Context and Problem Statement

The application multiplexes many long-lived cluster connections against one
window. It needs the kernel primitive that each platform actually has.

Qt does not give that primitive. Its portable loop is `poll()`
(`src/corelib/kernel/qeventdispatcher_unix.cpp`, `qt_safe_poll`); on macOS the
loop is a CFRunLoop, and no dispatcher exposes kqueue, epoll or IOCP. io_uring
appears nowhere in `qtbase`.

Two consequences follow. First, the transport cannot be built on
`QNetworkAccessManager` without inheriting `poll()` and Qt's six-connection
queue per host. Second, the backend must be selectable, because the right
primitive differs per platform and changes as the project reaches new targets.

## Decision Drivers

- The kernel primitive is a platform fact, not a product decision.
- Call sites must not name a backend.
- Readiness primitives and completion primitives must share one interface.
- A backend that cannot run must degrade, never abort.

## Considered Options

- **Interface oriented to completion** — `submitRead`/`submitWrite`/`submitAccept`;
  io_uring and IOCP are native, readiness backends transform.
- **Interface oriented to readiness** — natural for kqueue/epoll, forces a
  thread and a promise on io_uring and IOCP.
- **Reuse Qt Network** — no new code, inherits `poll()` and the queue.

## Decision Outcome

Chosen option: **a completion-oriented `IoEngine` port, with the backend chosen
at startup**.

`IoEngine` lives in `src/ports/io/`. Implementations live in
`src/adapters/platform/io/`. The domain names neither. A factory returns the
best backend the platform offers; call sites receive the port.

The interface owns the buffer: `submitRead` takes a `std::vector<std::byte>` by
value and returns it in the callback. Readiness backends cannot know when a
buffer is free, so ownership costs a copy and removes the lifetime bug class.

The engine owns exactly one thread. Callbacks run on it. Results cross into the
UI through a queued signal, never by direct call.

**Scope now: macOS.** The only production backend is `kqueue`. `poll` ships as
the portable reference and the conformance oracle; every backend must pass the
same suite.

**Deferred, with the port unchanged:** `epoll` and `io_uring` when Linux
starts, `iocp` when Windows starts. `io_uring` is detected at runtime through
`io_uring_setup` and falls back to `epoll` on `ENOSYS` or `EPERM`. `liburing`
links dynamically and only on Linux.

**`QNetworkAccessManager` is banned from the engine path.** One owner per
socket. Qt keeps the window system, timers and the UI thread.

### Consequences

- Good: the call site never learns the platform.
- Good: a new backend is an adapter, not a refactor.
- Good: `poll` keeps CI honest on every OS.
- Bad: the interface copies on read.
- Bad: `kqueue` is written before a real connection exists to prove it.
- Bad: two loop owners in the process — the engine thread and the Qt UI thread
  — so the handoff must be disciplined.
- Bad: `QNetworkAccessManager` is unavailable for ad-hoc requests.

## Superseded by ADR-0020

ADR-0020 removes the one-thread-per-engine rule: the engine is hosted in the Qt
main thread and stops owning a thread. Everything else in this record stands.


### Confirmation

`src/ports/io/` declares the port, `src/adapters/platform/io/` holds `kqueue`
and `poll`, and a conformance suite runs both. A call site that includes a
`QtNetwork` header under `src/adapters/` is a defect.
