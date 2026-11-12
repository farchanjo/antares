---
id: 01a1cbf4-8d08-7bc0-91c2-a11cedb15e55
number: 004
slug: harden-the-engine-loop
status: analyzed
created_at: 2026-11-11T18:52:44.168200694Z
---
# Feature Specification: Harden The Engine Loop

Feature: 004-harden-the-engine-loop
Created: 2026-11-11

## User Stories

- As the operator, I want a cancelled operation to leave nothing behind, so a
  long session does not crash on a stale registration.
- As the operator, I want a slow name lookup to settle only its own connection,
  so one slow host does not kill a healthy watch.
- As the operator, I want a shutdown to settle what is in flight, so the window
  never hangs on a spinner that will not finish.
- As the developer, I want the health counters and the backend name to be true,
  so a diagnosis starts from a fact.

## Scope

The engine loop and its store: cancellation, the timer queue, the task queue,
the resolver slot, the stop path, the backend name and the counters. No new
capability is added; the mode of operation does not change.

Out of scope: the hosted loop (`attach`/`runOnce`/`waitHandle`/`nextDeadline`/
`shutdown`), which ADR-0020 records separately, and anything above the engine.

Every item below is a defect already present in the tree, confirmed against the
source. None of them is theoretical.

## Functional Requirements

### FR-001 Cancellation removes the kernel registration

`KqueueEngine` overrides `forget` and removes the `EV_ONESHOT` registration
before the op is freed. `EpollEngine` and `IoUringEngine` already do. After a
`cancelPending` on a Darwin engine, the next readable byte on that descriptor
must not reach a freed `PendingOp`.

### FR-002 Resolution settles once per job

`claimResolve` keys on the job, not on a shared slot. Two lookups of the same
host inside the watchdog window each settle their own connection, and neither
can complete the other's.

### FR-003 The task queue is lock-free and multi-producer

`post` is safe from any thread and admits several producers: the resolver, a
parse worker returning a batch, a connection, and the loop itself. The engine is
one loop (FR-018 of 002), so the queue is the only thing several threads touch
and it is the only thing that needs to be atomic. The queue is an intrusive singly linked list
whose head is a single atomic; a push is a compare-exchange loop and a drain is
one exchange, so neither `post` nor `nextWaitMilliseconds` takes a mutex.

`fireDueTimers` pushes through the same `post` path, so the two mutexes that
guard one deque today collapse to none. `nextWaitMilliseconds` reads the head
with acquire and takes no lock.

### FR-004 The op store is read under its lock

`cancelPending` walks `ops_` under `opsMutex_`. A concurrent `release` from the
loop cannot invalidate the walk.

### FR-005 Stop settles what is pending

`stop()` completes every pending op with `IoError::cancelled` before returning,
instead of dropping them. A completion runs at most once per op, and the HTTP
client maps it to `HttpError::cancelled`.

### FR-006 Stop is reversible

`stopped_` returns to false and the loop can run again after a `stop()`. The
contract in `engine_base.hpp` says "until `run()`" and must be true.

### FR-007 The reported backend is the chosen one

`selectedBackendName` reports `io_uring` when the ring is the engine in use, and
`epoll` only when it is. The name feeds the log and the conformance report.

Today it reports `epoll` on the default path even when the ring was built, which
was proved by constructing the factory and comparing the reported name against
the concrete type: the two diverge. A case asserts they agree for each forced
value, so the name can never drift from the engine again.

### FR-010 A ring that cannot open falls back to epoll

On Linux the factory tries `io_uring` first and `epoll` second, and lands on
`epoll` when the ring cannot be set up — a kernel with `io_uring_disabled`, or a
seccomp profile that blocks `io_uring_setup`. The run continues; it does not fail
and it does not silently drop to `poll`.

A preloaded shim makes `io_uring_setup` return `ENOSYS`, and the case asserts the
engine is an `EpollEngine` and that the run completes. The shim is test-only, so
production keeps no switch for it.

### FR-009 A drain stops at the turn deadline

`drainTasks` takes the deadline of the turn, runs tasks until it is reached, and
returns the rest to the head in their original order. A burst of 5 000 tasks
therefore does not run inside one turn.

The remaining tasks keep FIFO order: the drain reverses the list it exchanged out
and prepends what it did not run with one compare-exchange, so no task overtakes
another and none is dropped.

### FR-008 Counters move once per event

Every counter incremented on a completion path moves exactly once per op, and
`stats()` is consistent with the number of ops the engine accepted.

## Security Requirements

- **Data sensitivity/classification.** The engine moves opaque bytes plus the
  descriptor number. Nothing here is a credential; the credential stays above in
  the HTTP layer.
- **Authentication/authorization.** Not applicable — the engine holds descriptors
  the caller opened and decides nothing about identity.
- **Input validation.** The untrusted input is the descriptor number and the
  size. A cancelled op must not be completed twice, which FR-001 and FR-002
  bound; a zero-size read is still rejected before submission.
- **Cryptography in transit/at rest.** Not applicable — the port carries
  ciphertext without inspecting it.
- **Logging/audit.** Cancellation and timeout are logged at `warn` with the
  backend name and the error family only. No buffer content and no path.
- **Error-handling information exposure.** `IoError` carries the `errno` value
  and not a formatted system string, so a message cannot leak a path or a host.

## Acceptance Scenarios

Given a Darwin engine
When  an op is cancelled and the descriptor becomes readable
Then  the completion runs once and no freed op is touched.

Given two lookups of the same host inside the watchdog window
When  both resolve
Then  each connection settles once.

Given a pending op
When  `stop()` returns
Then  the op completed with `cancelled` and never completes again.

The executable form lives in
`doc/arch/specs/features/harden-the-engine-loop.feature`.

## Observability

No new signal. The existing families gain accuracy: `EngineStats.submitted`,
`completed` and `failed` stop undercounting cancelled ops, and the backend name
in the log becomes the real one (FR-007, FR-008). Cardinality is unchanged —
the labels stay `backend` and `error`, both bounded.

## Clarifications
