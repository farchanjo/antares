# Tasks: Open The Window

## Task Breakdown

- [ ] T001 Add the `antares_app` executable target and make `antares_ui` a real
      library instead of INTERFACE (FR-001).
- [ ] T002 Write `Application::build()` in dependency order with
      `std::expected<void, StartupError>` and the unwind on failure (FR-001,
      FR-002).
- [ ] T003 `attach()` the engine and install the `waitHandle()` notifier driving
      `runOnce(nextDeadline())` (FR-003).
- [ ] T004 Install `SIGTERM`/`SIGINT` through the self-pipe and `SIG_IGN` for
      `SIGPIPE`; set the pipe non-blocking (FR-005).
- [ ] T005 Read `current-context`; on absence open an empty window naming the
      file (FR-006).
- [ ] T006 Write `Application::shutdown()`: uninstall the handler, `drain()`,
      destroy in reverse with the engine last (FR-004).
- [ ] T007 Open the kubeconfig read-only and add the case that two instances
      start without writing it (FR-007).
- [ ] T008 Case: build against a fixture, assert the window and both notifiers,
      shut down and assert the order (FR-001, FR-004).
- [ ] T009 Case: raise `SIGTERM` and assert `exec()` returns within the bound
      (FR-005).

## Dependencies

T001 blocks everything. T003 needs `attach`/`waitHandle`/`nextDeadline` from
feature 007's hosted loop. T006 needs `drain()` from 002 FR-026. T008 and T009
need T002 and T006.
