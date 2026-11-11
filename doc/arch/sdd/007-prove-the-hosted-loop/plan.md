# Implementation Plan: Prove The Hosted Loop

## Overview

Seven measurements on the host, one file of throwaway glue, one written result.
Nothing lands in `src/` that the product keeps.

## Technical Approach

**Harness.** A single `main.cpp` under `doc/arch/spikes/hosted-loop/`: a
`QApplication`, one `QLabel` counting painted frames, a loopback listener, and a
`QSocketNotifier` over the kqueue fd that the engine's `attach()` would expose.
Built by a two-line `clang++` invocation recorded in the spike's `README.md`, not
by the project's `CMakeLists.txt`.

**FR-001.** Register one read through the engine on a connected socket, arm the
notifier, have the peer write, and count wakes. No timer is armed, so a wake can
only come from the notifier.

**FR-002.** Run the transport at full rate for 30 s and count painted frames
against a 30 s idle baseline taken first.

**FR-003.** Open a `QMessageBox` from a completion, dismiss it after 2 s with a
timer, and assert the transport completed during the modal.

**FR-004.** Drag the window edge for 10 s, then hold a menu open for 10 s, both
while a byte stream runs; count delivered events against sent.

**FR-005.** Submit a 200 ms timer through the engine and record the firing delay
on the main thread.

**FR-006.** Push a completion that sleeps 50 ms and record p99 turn duration and
overruns against a 8 ms budget.

**FR-007.** Occlude the window for 60 s with a 1 s timer armed, then repeat with
`NSProcessInfo beginActivity` held, and compare the two.

### Risks

- The host may lack a Qt6 with the same ABI as the spike's `-stdlib`; the README
  records the working invocation.
- A saturated loopback can starve the paint for reasons unrelated to the design;
  FR-002 measures against an idle baseline on the same machine, not a fixed FPS.
- App Nap measurement is noisy; FR-007 compares two runs, not an absolute.

## Constitution

- **P-05 — Darwin first.** The spike runs only on the acceptance platform.
- **P-03 — determinism.** Thresholds are ratios against a same-machine baseline,
  not wall-clock absolutes.
- **D-13 — offline.** Loopback only; no network beyond the host.
- **P-13 — leaks.** The spike is run under `leaks` once, and a leak is a result.

## Companion Artifacts

- `doc/arch/spikes/hosted-loop/README.md` — how to build and run, and the
  invocation that worked.
- The numbers go into ADR-0020's "More Information" when it is accepted.
