# Implementation Plan: Harden The Engine Loop

## Overview

Eight defects, all reachable from the current code and none covered by a test.
The plan closes each one at its site in `src/adapters/platform/io/` and adds the
conformance case that would have caught it. No capability is added and `run()`
stays the driver, so ADR-0020 lands later on a base the suite can hold.

## Technical Approach

Every change is inside the adapter layer, below the port. The port contract does
not move.

**FR-001 — the Darwin UAF.** `KqueueEngine` gains a `forget` override that
issues `EV_DELETE` for the op's descriptor and filter, mirroring what
`EpollEngine::forget` does with `EPOLL_CTL_DEL` and `IoUringEngine::forget` with
`poll_remove`. The base stays a no-op for backends with nothing to unregister.

**FR-002 — the resolve slot.** `resolveSettled_` becomes per-job state: the
shared `ResolveJob` carries its own `std::atomic<bool> settled`, and
`claimResolve` exchanges on that instead of on an engine-wide slot. The watchdog
lambda captures the job's own flag, so a later job cannot make an older one
conclude it was never claimed.

**FR-003 and FR-004 — the two locks.** `fireDueTimers` collects due tasks while
holding `timerMutex_` and appends them under `mutex_`; `nextWaitMilliseconds`
reads `tasks_` under `mutex_`. `cancelPending` takes `opsMutex_` for its walk and
copies the doomed pointers before completing any of them, because the completion
can re-enter `release`.

**FR-005 — stop settles.** `stop()` reuses the switch `cancelPending` already
has, completing each pending op with `IoError::cancelled` before the resolver
join. `HttpClient` maps `IoError::cancelled` to `HttpError::cancelled`, so the
completion is terminal and the pending counter reaches zero.

**FR-006 — stop is reversible.** `stopped_` becomes a plain flag cleared by
`run()`'s entry instead of an exchange that never resets. The guard against a
double `stop()` moves to a separate one-shot.

**FR-007 and FR-008 — the truthful name and the counters.** `selectedBackendName`
follows the same branch the factory takes. `countCompletion` runs on the
cancellation path too, so `completed` and `failed` stop undercounting.

### Test strategy

One conformance case per FR in `tests/io/conformance.cpp`, each reaching the
defect without a sleep. The cancellation cases need a descriptor that stays open
(the contract says `cancelPending` does not close it) and a write after the
cancel; the resolve case needs two submissions inside the watchdog window.

### Risks

- `stop()` completing changes what the leak pass observes; the leak target needs
  a case that cancels and then stops.
- `EV_DELETE` on an already-delivered `EV_ONESHOT` returns `ENOENT`; a `forget`
  that treats it as failure breaks the epoll-parity case.
- Holding `opsMutex_` across a completion is a re-entrancy hazard; the copy
  before completing is what keeps it out.

## Constitution

- **D-08 — recoverable absence never panics.** Every new failure path returns
  `IoError`; no `unwrap`-style abort appears in the adapter.
- **P-13 — every run is leak-checked.** The new `stop()` path is exercised under
  the leak target, which is what proves the cancellation completes rather than
  drops.
- **P-12 — ownership is explicit.** The doomed list holds non-owning
  `PendingOp *` only for the duration of the call; ownership stays in `ops_`.
- **D-05 — no magic numbers.** The `EV_DELETE` filter and the `ENOENT` case are
  named constants, not literals.
- **D-02 — the project validates itself.** Each FR gets a conformance case, so
  the fix is proved by the same suite the project dogfoods.

## Companion Artifacts

- `contracts/` — none; the port contract does not move, so the CUE schema in
  `doc/arch/schemas/harden-the-engine-loop.cue` is the whole surface.
- `research.md` — not needed; the defects are already cited line by line in
  ADR-0020.
