---
status: proposed
date: 2026-11-11
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0020 — Host the engine loop in the Qt main thread

## Context and Problem Statement

The product is a desktop app with no UI yet, and the UI is Darwin-only for the
first cut (P-05). Linux ships the engine and its factory, nothing above them.

Every call the app makes is already asynchronous: `IoEngine` completes on its
loop thread, `ports::http::Client` hands the completion to the loop, and
`Ticket::cancel` is idempotent. What does not exist is a place for the loop to
run next to a window.

ADR-0006 decided the opposite of what this record decides: "The engine owns
exactly one thread. Callbacks run on it. Results cross into the UI through a
queued signal, never by direct call", and it listed "two loop owners in the
process" as a cost. That shape works and it is what a headless client wants.

It is also wrong for a UI. Two blocking loops in one thread cannot coexist —
only one may sleep. If the engine sleeps in `kevent`, Cocoa input arrives on a
Mach port that no code of ours reads, and the window freezes. If the Qt loop
sleeps, a ready socket wakes nothing.

## Decision Drivers

- One blocking loop per process, so a sleep is unambiguous.
- Delivery on the main thread, because the model is there.
- The domain must not learn Qt, so the port stays toolkit-free.
- The hosted mode must be provable without Qt, or it is only ever exercised on
  one machine.
- `poll` must keep working: it is the escape hatch (ADR-0009).

## Considered Options

- **Engine on its own thread, handoff by queued signal.** ADR-0006 as written.
- **Custom `QAbstractEventDispatcher`.** Replace the Qt dispatcher with ours.
- **Engine hosted in the Qt loop.** `QSocketNotifier` on the backend fd,
  non-blocking `runOnce()`, `app.exec()` as the only sleep.
- **Engine pumping `processEvents`.** The engine loop stays and calls Qt.

## Decision Outcome

Chosen option: **the engine is hosted in the Qt main thread.**

The engine stops owning a thread and becomes a source inside someone else's
loop. Five capabilities join `IoEngine`, taking it to 14 virtuals; there is no
second port, because the hosted mode is internal and the split would be
decoration:

| Capability | Why |
|------------|-----|
| `attach()` | records the loop thread and arms the wakeup, without the loop of `run()` |
| `runOnce(budget)` | one non-blocking turn: drain wakeup, drain tasks, fire due timers, poll once with a zero timeout |
| `waitHandle()` | the fd the host watches |
| `nextDeadline()` | the nearest timer deadline, or nothing |
| `shutdown()` | completes every pending op with `cancelled`, then stops |

`waitHandle()` returns the backend fd: `queue_`, `set_`, or `ring_->ring_fd`
(`/usr/include/liburing.h:132`). kqueue and epoll fds become readable while an
event is pending; a pending CQE does the same for the ring. `PollEngine` has no
aggregate fd and returns the **wakeup pipe** (`engine_base.hpp:135`); it is the
only backend whose readiness has no signal, so it degrades to one sweep per
turn, bounded by `nextDeadline()`.

Timers have no fd at all — `timers_` is a vector in memory and `fireDueTimers`
only runs inside `run()`. Without `nextDeadline()` driving a host timer, retry,
the resolve watchdog, the WebSocket close timer and the idle timeout never fire
in hosted mode.

The wakeup pipe carries every `submit*`, `post`, `submitTimer` and `stop`
(`engine_base.cpp:107,136,157,200,241,382,425`), not only `post`. Readiness
never touches it, except on the poll backend where it is the only signal.

`io_uring` needs a flush and a rearm at the **end** of every turn: `arm()` only
queues, the wake request is rearmed at the top of `waitReady` and cleared when
consumed (`io_uring_engine.cpp:104-106,165-169,204-205`), so a `post` arriving
after the turn finds nothing armed.

Delivery to the model is queued even on the same thread. A completion that opens
a modal re-enters the turn with the outer `events[]` array alive, and an inner
`cancelPending` frees ops that array points at.

`HttpClient` takes `IoEngine&` instead of `EnginePool&`; its destructor decides
with `engine_.isEngineThread()`. With one loop there is nothing for a pool to
choose, so `EnginePool` drops to a convenience wrapper for the tools
(`tools/k8s_live`, `tools/bench_io`). `BlockingClient` is removed: a caller
awaits on its own condition variable, so the client needs no sync facade.

Hosting one loop is what makes the single-thread contract of ADR-0006 literal:
there is no second loop to share `sessions_`, `idle_` or `stats_` with.

It also moves the shutdown. Once the loop is the event loop, nothing processes it
after `app.exec()` returns, so `drain()` runs the loop instead of waiting on it
(FR-026 of 002). `SIGTERM` and a window close both reach it through `quit()`, and
`aboutToQuit` only marks the client closed, because it runs inside the loop.

The hosted mode is proved without Qt: a `poll()`-based host in `tests/io/`
drives `runOnce()` over `waitHandle()` and `nextDeadline()` on all four
backends, so the Linux suite gates it. The `QSocketNotifier` glue is Darwin
only and is proved by a spike on the macOS host before this record is accepted.

### Consequences

- Good: one sleep, and it is `app.exec()`.
- Good: the hosted mode is testable on Linux, where the suite runs.
- Good: no thread hop for the model, which lives on the main thread.
- Bad: TLS, HPACK and JSON of a large LIST run on the main thread; a turn needs
  a budget, and spec 003 FR-002 (`limit`/`continue`) becomes mandatory.
- Bad: a handler that blocks freezes the window, not just the transport.
- Bad: re-entrancy is a real hazard; every dispatch point needs a depth guard.
- Bad: `PollEngine` hosted costs one sweep per turn while an op is pending.
- Bad: the destructor chain is not imposable; `shutdown()` must be called
  explicitly, and the resolver thread must not be joined on the main thread.

## Pros and Cons of the Options

### Engine on its own thread, handoff by queued signal

ADR-0006 as written. Costs a hop per completion and a queue that outlives both
sides; buys isolation. Reconsider for a second window or a CPU-bound view.

### Custom `QAbstractEventDispatcher`

One loop, and it is the Qt one. Costs an integration that is hard to unit-test
and breaks on Qt upgrades; the cocoa plugin imposes its own dispatcher.

### Engine pumping `processEvents`

Looks equivalent and is not: `processEvents` inside the loop nests unbounded,
and the `events[]` array of the outer frame is freed under an inner one.

## More Information

Supersedes the thread clause of ADR-0006 (":54-55"), and the "two loop owners"
cost it recorded (":76"). ADR-0007 and ADR-0013 assume one thread per engine and
need the same note. ADR-0009 said a refused queue must degrade and never abort;
this record keeps that by hosting `poll` instead of refusing it.

Feature 004 supersedes the contradicted requirements: spec 001 FR-006, FR-007,
FR-013; spec 002 FR-001, FR-002, FR-007, FR-016. The revocations are recorded
on the features themselves.

To fix before the first window — all confirmed: `KqueueEngine` never overrides
`forget` (`kqueue_engine.hpp:23-28`), so a cancelled op leaves an `EV_ONESHOT`
registration pointing at freed memory; `claimResolve` uses one slot for all jobs
(`engine_base.cpp:274-277`), so two lookups inside the watchdog window settle
the same connection twice; `fireDueTimers` pushes into `tasks_` under
`timerMutex_` (`:433,444`) while `post` uses `mutex_` (`:379`) and
`nextWaitMilliseconds` reads it unlocked (`:455`); `cancelPending` walks `ops_`
unlocked (`:349`); `stop()` drops pending ops instead of completing them
(`:405-406`); `stopped_` never returns to false (`engine_base.hpp:207`);
`selectedBackendName` reports "epoll" when io_uring was chosen
(`engine_factory.cpp:40-45`).

Also left open by this record: the Linux build links `-stdlib=libc++`
(`CMakeLists.txt:29-30`) while a distro Qt is built against libstdc++, so the
two do not share an ABI. It does not bite yet because no UI is built on Linux;
it bites on the first Linux UI build.
