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
- [ ] T005 Replace `tasks_` with an intrusive MPSC list: atomic head, push by
      compare-exchange, drain by one exchange and a reverse (FR-003).
- [ ] T006 Route `fireDueTimers` through `post` and read the head atomically in
      `nextWaitMilliseconds` (FR-003).
- [ ] T007 Add the case `queue/four-producers`: four threads post while the loop
      drains and a timer fires; every task runs once (FR-003).
- [ ] T008 Give `drainTasks` the turn deadline and prepend the unrun tail with
      one compare-exchange; case `queue/deadline-keeps-order` over 5 000 tasks
      (FR-009).
- [ ] T009 Take `opsMutex_` in `cancelPending`, copy the doomed pointers, and
      complete after releasing (FR-004).
- [ ] T010 Add the case `cancel/walks-under-lock`: cancel while the loop
      releases an op (FR-004).
- [ ] T011 Make `stop()` complete every pending op with `IoError::cancelled`
      before the resolver join, reusing the `cancelPending` switch (FR-005).
- [ ] T012 Map `IoError::cancelled` to `HttpError::cancelled` in `HttpClient`
      so the completion is terminal (FR-005).
- [ ] T013 Add the case `stop/settles-pending`: an op in flight at `stop()`
      completes once with `cancelled` (FR-005).
- [ ] T014 Clear `stopped_` on `run()` entry and move the double-`stop()` guard
      to a one-shot (FR-006).
- [ ] T015 Add the case `stop/reversible`: `run()` after `stop()` serves work
      again (FR-006).
- [ ] T016 Follow the factory branch in `selectedBackendName` so io_uring
      reports itself (FR-007).
- [ ] T017 Add the case `factory/backend-name`: the forced backend and the
      reported name agree (FR-007).
- [ ] T018 Call `countCompletion` on the cancellation path so `completed` and
      `failed` stop undercounting (FR-008).
- [ ] T019 Add the case `stats/cancel-counts`: a cancelled op moves the counters
      once (FR-008).
- [ ] T020 Extend the leak target with a cancel-then-stop case, since `stop()`
      completing changes what the pass observes.

## Dependencies

None external. Every task is inside `src/adapters/platform/io/` and
`tests/io/`; the port contract does not move. T009 and T012 are the pair
ADR-0020 depends on, so they must land before the hosted loop is planned.

T021 depends on nothing and should land early: it is the case that proves the
name honest, and it fails today. T022 and T023 are Linux-only, so a Darwin run
skips them and the shim is not loaded there.
