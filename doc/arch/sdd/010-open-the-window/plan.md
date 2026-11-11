# Implementation Plan: Open The Window

## Overview

One `main.cpp` and one `Application` class. The class builds in dependency order,
hosts the loop over `QSocketNotifier`, and tears down in reverse after the drain.
Nothing here has behaviour of its own.

## Technical Approach

**Where it lives.** `src/adapters/ui/main.cpp` and `src/adapters/ui/application.{hpp,cpp}`,
with a new `antares_app` executable target linking `antares_ui` plus the three
libraries. `antares_ui` stops being INTERFACE-only.

**FR-001 — order.** `Application::build()` creates in the stated order and returns
`std::expected<void, StartupError>`. Every `QObject` is `new`-ed with its parent.
The engine is `std::unique_ptr<IoEngine>`.

**FR-002 — unwind.** `build()` is written so each step records whether it
completed; on failure the reverse runs for the completed steps only. `main`
prints the family and returns non-zero.

**FR-003 — host.** After `show()`, `attach()` and the two notifiers — one over
`waitHandle()`, one over the self-pipe — are installed. The wakeup notifier calls
`runOnce(deadline)` with `nextDeadline()`.

**FR-004 — teardown.** An `Application::shutdown()` runs the reverse: uninstall
the signal handler, `drain()`, then destroy in reverse. `main` calls it after
`exec()`, never before.

**FR-005 — signals.** `sigaction` for `SIGTERM`/`SIGINT` with a handler that only
`write`s the pipe; `SIG_IGN` for `SIGPIPE`. The pipe is created with `pipe2`/
`pipe` and set non-blocking.

**FR-006 — first context.** The kubeconfig's `current-context` is read at build
time. Absent is a normal outcome: the window shows the message and stays usable.

**FR-007 — read-only.** The kubeconfig is opened read-only; nothing writes it.

### Test strategy

The application class is testable with `QApplication` in offscreen mode: build
against a fixture kubeconfig, assert the window exists and the notifiers are
installed, then shut down and assert the order. A `SIGTERM` case raises the
signal and asserts `exec()` returns.

### Risks

- Two notifiers and an event loop is exactly where a shutdown order bug hides;
  the teardown case is the guard.
- `attach()` must run before any post, or a completion lands on a thread that is
  not the loop.
- The self-pipe must be non-blocking, or a full pipe blocks the handler.

## Constitution

- **P-12 as amended** — children take parents; the engine, not a `QObject`, is
  `unique_ptr`.
- **P-05** — the entry point is macOS-first; `signalfd` is not used.
- **P-10** — startup logs the context at `info`; a drain expiry logs `warn`.
- **D-15** — no new label; the context name is a value, not a dimension.
- **D-03** — the entry point is `adapters`; it may import `application` and
  `ports`.

## Companion Artifacts

- `contracts/` — none; this is a process, not a service.
- `data-model.md` — not needed; `open-the-window.cue` names the startup outcome.
