# Tasks: Kernel Io Engine Kqueue

## Task Breakdown

- [x] T001 Declare `IoError` and the seven port operations in
      `src/ports/io/io_engine.hpp`; no definition, header only (FR-001, FR-005).
- [x] T002 Declare `PendingOp` and the stable-address operation store in
      `src/ports/io/pending_op.hpp` (FR-002).
- [x] T003 Write `src/application/io/engine_factory.cpp` selecting by platform
      and honouring `ANTARES_IO_BACKEND`; expose `name()` (FR-004).
- [x] T004 Implement the `poll` backend in
      `src/adapters/platform/io/poll_engine.cpp` (FR-001, FR-008).
- [x] T005 Implement the engine thread, the self-pipe wakeup and `post()`
      (FR-007).
- [x] T006 Implement the kqueue backend in
      `src/adapters/platform/io/kqueue_engine.cpp` with `EV_ONESHOT` (FR-003).
- [x] T007 Map `EV_EOF` to `closed` and `EV_ERROR` to `system` (FR-005).
- [ ] T008 Implement `cancel()` so the callback fires once with `cancelled` and
      NOTE: the client ticket cancels today (closes the h1 connection, resets the
      h2 stream); the engine still has no op-level cancel, so a pending read on a
      kept-alive socket cannot be withdrawn without closing it.
      never again (FR-006).
- [x] T009 Guarantee no callback after `stop()` returns; join the engine thread
      (FR-006, FR-007).
- [x] T010 Add the conformance suite `tests/io/conformance.cpp` that takes
      `IoEngine&` and names no backend (FR-008).
- [x] T011 Instantiate the suite twice through CTest, one per backend (FR-008).
- [x] T012 Add fault injection for `tooManyDescriptors` through a lowered
      `RLIMIT_NOFILE` (FR-005).
- [x] T013 Wire `antares_io` into CMake with the kqueue sources behind
      `if(APPLE)`; add the suite to the `dev` test preset (FR-003, FR-008).
- [x] T014 Add the counters named in the spec's Observability section and read
      them from a suite case (FR-001).

- [x] T015 Declare `IoHandle` with `fstat` classification and RAII close in
      `src/ports/io/io_handle.hpp` (FR-009).
- [x] T016 Implement adoption, `openFile` and ownership transfer in
      `src/ports/io/io_handle.cpp` (FR-009).
- [x] T017 Retarget every port operation to `const IoHandle &` plus an optional
      offset (FR-009, FR-011).
- [x] T018 Give the `poll` backend the immediate strategy for `file` handles and
      cover it with a suite case (FR-010).
- [x] T019 Give the kqueue backend the same immediate strategy and assert the
      loop stays idle on a file read (FR-010).

- [x] T020 Force kqueue in the factory and reach `poll` only by kernel refusal
      or `ANTARES_IO_BACKEND` (FR-004).
- [x] T021 Log the selected backend name once at startup (FR-004).

## Dependencies

- The `dev` preset and `lld` on the host — done.
- `kqueue` needs a Darwin host; `poll` and the suite run anywhere, so T004 and
  T010 land before T006 is provable on the host.
- No new library: `kqueue` and `poll` come from libc.

