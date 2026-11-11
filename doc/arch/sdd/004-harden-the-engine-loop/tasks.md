# Tasks: Harden The Engine Loop

Each task lands with its conformance case; a fix without the case that would
have caught it is not done. Order follows the risk: the Darwin UAF first.

## Task Breakdown

- [ ] T001 Add the failing case `kqueue/forget-drops-registration`: cancel a
      read, then make the descriptor readable, and assert no completion reaches
      the freed op (FR-001).
- [ ] T002 Override `forget` in `KqueueEngine` with `EV_DELETE` for the op's
      descriptor and filter, tolerating `ENOENT` (FR-001).
- [ ] T003 Add `settled` to the shared resolve job and key `claimResolve` on it
      instead of the engine-wide slot (FR-002).
- [ ] T004 Add the case `resolve/two-jobs-one-window`: two lookups of the same
      host inside the watchdog, both settle once (FR-002).
- [ ] T005 Collect due tasks under `timerMutex_` and append them under `mutex_`;
      read `tasks_` under `mutex_` in `nextWaitMilliseconds` (FR-003).
- [ ] T006 Add the case `timers/post-during-fire`: a post from the resolver while
      `fireDueTimers` runs loses no task (FR-003).
- [ ] T007 Take `opsMutex_` in `cancelPending`, copy the doomed pointers, and
      complete after releasing (FR-004).
- [ ] T008 Add the case `cancel/walks-under-lock`: cancel while the loop
      releases an op (FR-004).
- [ ] T009 Make `stop()` complete every pending op with `IoError::cancelled`
      before the resolver join, reusing the `cancelPending` switch (FR-005).
- [ ] T010 Map `IoError::cancelled` to `HttpError::cancelled` in `HttpClient`
      so the completion is terminal (FR-005).
- [ ] T011 Add the case `stop/settles-pending`: an op in flight at `stop()`
      completes once with `cancelled` (FR-005).
- [ ] T012 Clear `stopped_` on `run()` entry and move the double-`stop()` guard
      to a one-shot (FR-006).
- [ ] T013 Add the case `stop/reversible`: `run()` after `stop()` serves work
      again (FR-006).
- [ ] T014 Follow the factory branch in `selectedBackendName` so io_uring
      reports itself (FR-007).
- [ ] T015 Add the case `factory/backend-name`: the forced backend and the
      reported name agree (FR-007).
- [ ] T016 Call `countCompletion` on the cancellation path so `completed` and
      `failed` stop undercounting (FR-008).
- [ ] T017 Add the case `stats/cancel-counts`: a cancelled op moves the counters
      once (FR-008).
- [ ] T018 Extend the leak target with a cancel-then-stop case, since `stop()`
      completing changes what the pass observes.

## Dependencies

None external. Every task is inside `src/adapters/platform/io/` and
`tests/io/`; the port contract does not move. T009 and T012 are the pair
ADR-0020 depends on, so they must land before the hosted loop is planned.
