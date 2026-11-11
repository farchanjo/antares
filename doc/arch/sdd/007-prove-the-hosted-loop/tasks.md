# Tasks: Prove The Hosted Loop

## Task Breakdown

- [ ] T001 Write the spike `main.cpp` and its `README.md` with the build
      invocation; confirm it builds on the host.
- [ ] T002 Measure FR-001: notifier wakes on a kqueue fd with no timer armed.
- [ ] T003 Measure FR-002: painted frames against an idle baseline under a
      saturated transport.
- [ ] T004 Measure FR-003: a modal opened from a completion, dismissed by timer.
- [ ] T005 Measure FR-004: events delivered during a live resize and a held menu.
- [ ] T006 Measure FR-005: firing delay of a 200 ms engine timer on the main
      thread.
- [ ] T007 Measure FR-006: p99 turn duration and overruns against the 8 ms budget.
- [ ] T008 Measure FR-007: timer latency occluded, with and without an activity
      assertion.
- [ ] T009 Run once under `leaks` and record the result.
- [ ] T010 Paste the numbers into ADR-0020 and move it from `proposed`, or record
      the failure and the fallback taken.

## Dependencies

T001 blocks everything. T002 is the load-bearing one: a failure there stops the
run and switches the approach to `CFFileDescriptor` or `dispatch_source`. T010
needs every measurement; a partial set does not move the ADR.

The spike needs the macOS host `farchanjo@192.168.202.241` and a Qt6 there; the
workstation cannot run it.
