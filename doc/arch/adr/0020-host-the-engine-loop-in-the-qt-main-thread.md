---
status: accepted
date: 2026-11-11
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0020 — Host the engine loop in the Qt main thread

## Context and Problem Statement

The product is a desktop app with no UI yet. Every call the app makes is already
asynchronous: `IoEngine` completes on its loop thread, `ports::http::Client`
hands a completion to the loop, and `Ticket::cancel` is idempotent. What does
not exist is a place for the loop to run next to a window.

ADR-0006 decided the opposite of what this record decides: "The engine owns
exactly one thread. Callbacks run on it. Results cross into the UI through a
queued signal, never by direct call", and it listed "two loop owners in the
process" as a cost. That shape works, and it is what a headless client wants.

It is also the wrong default for a UI. Two blocking loops in one thread cannot
coexist — only one may sleep. If the engine sleeps in `kevent`, Cocoa input
arrives on a Mach port that no code of ours reads, and the window freezes. If
the Qt loop sleeps, a ready socket wakes nothing, and the view only refreshes
when the mouse moves. A short timeout in both is a busy-wait with extra steps.

A UI also cannot wait for a thread hop per keystroke. The list model, the
selection and the scroll position all live on the main thread, and every
`QueuedConnection` hop is a copy and a latency the interaction pays for.

## Decision Drivers

- One blocking loop per process, so a sleep is unambiguous.
- Delivery on the main thread without a hop, because the model is there.
- Determinism: the loop must be drivable from a test, with no sleeping.
- The domain must not learn Qt, so `Channel`/`Event` types stay outside.
- The `poll` backend must keep working, because it is the escape hatch.

## Considered Options

- **Engine on its own thread, handoff by queued signal.** ADR-0006 as written.
- **Custom `QAbstractEventDispatcher`.** Replace the Qt dispatcher with ours.
- **Engine hosted in the Qt loop.** `QSocketNotifier` on the backend fd,
  non-blocking `runOnce()`, `app.exec()` as the only sleep.
- **Engine pumping `processEvents`.** The engine loop stays and calls Qt.

## Decision Outcome

Chosen option: **the engine is hosted in the Qt main thread.**

The engine stops owning a thread and becomes a source inside someone else's
loop. Four capabilities are added to a separate port,
`ports/io/hosted_loop.hpp`, so `IoEngine` does not grow past its 13 virtuals
(D-05 `interface.fat`):

| Capability | Why |
|------------|-----|
| `attach()` | records the loop thread before the first pump and arms the wakeup, without the blocking prologue of `run()` |
| `runOnce(budget)` | one non-blocking turn: drain wakeup, drain tasks, fire due timers, poll once with a zero timeout |
| `waitHandle()` | the fd the host watches — the **backend** fd, not the wakeup pipe |
| `shutdown()` | completes every pending op with `cancelled`, then stops |

`waitHandle()` returns the kqueue, epoll or io_uring ring fd. The wakeup pipe
carries only `post` and `submitTimer`; socket readiness never touches it, so a
notifier on the pipe alone would stall exactly the traffic that matters. Both
kqueue and epoll fds become readable while an event is pending, which is what
makes one notifier sufficient. `PollEngine` has no aggregate fd and is therefore
refused in hosted mode (ADR-0009).

The host installs the notifier **after** `QApplication` exists and destroys it
**before** `QApplication` dies. `attach()` cannot reuse the prologue of `run()`:
`tuneLoopThread` renames the thread and changes its QoS, and the main thread
already has both from AppKit.

Delivery to the model is always queued, even on the same thread. A completion
that opens a modal re-enters the turn with the outer `events[]` array alive, and
an inner `cancelPending` frees ops that array still points at.

Timers are not coalesced and are not owned by the UI: `submitTimer` gains a
cancellable id, and the idle deadline for a watch is rearmed per stream, because
the h2 connection is shared and `Connection::onData` is a single slot.

### Consequences

- Good: one sleep. `app.exec()` is the only blocking call; a socket and a
  keystroke are the same kind of wakeup.
- Good: no hop for interaction — the model, the widget and the completion share
  a thread by construction.
- Good: deterministic tests. `runOnce()` drives the engine with no `sleep_for`,
  which is the rule the project already holds itself to.
- Bad: TLS, HPACK and JSON decoding of a large LIST now run on the main thread,
  so a turn needs a budget and a large list needs slicing or a worker.
- Bad: a handler that blocks stalls the window, not just the transport.
- Bad: `PollEngine` drops out of hosted mode and needs a thread of its own or a
  refusal at startup.
- Bad: re-entrancy becomes a real hazard; every dispatch point needs a depth
  guard and a queue.

## Pros and Cons of the Options

### Engine on its own thread, handoff by queued signal

The ADR-0006 shape. Costs a hop per event, a second thread, and a queue whose
lifetime outlives both sides. Buys isolation: a slow handler delays only the
transport. Reconsider for a second window, or for a CPU-bound view.

### Custom `QAbstractEventDispatcher`

One loop, and it is the Qt one, so everything is native. Costs an integration
that is hard to unit-test and breaks on Qt upgrades. The cocoa plugin imposes
its own dispatcher, so this is a fork of the platform plugin, not a setting.

### Engine pumping `processEvents`

`processEvents` inside the engine loop looks equivalent and is not: `pump` and
`processEvents` both execute arbitrary code, so nesting is unbounded and the
`events[]` array of the outer frame is freed under an inner one.

## More Information

Supersedes the thread clause of ADR-0006 and the "two loop owners" cost it
recorded. Related: ADR-0006 (the port), ADR-0009 (poll as the fallback),
ADR-0018 (Linux backend order), ADR-0013 (the HTTP client).

Confirmed by a spike on Darwin: `QSocketNotifier` over a `kqueue` fd, with
`dispatch_source` on the main queue as the fallback. `io_uring` submits its ring
only inside `waitReady`, so `runOnce` has to submit there too.

Known defects this decision exposes, to be fixed before the first window:
`KqueueEngine` never overrides `forget`, so a cancelled op leaves an `EV_ONESHOT`
registration pointing at freed memory; `claimResolve` uses one slot for all jobs,
so two lookups inside the watchdog window settle the same connection twice;
`fireDueTimers` pushes into `tasks_` under `timerMutex_` while `post` uses
`mutex_`; `stop()` drops pending ops instead of completing them.
