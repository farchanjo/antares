---
id: 01a1cbf4-b82e-7da1-94f3-6d5a0b2e8c40
number: 007
slug: prove-the-hosted-loop
status: analyzed
created_at: 2026-11-11T21:35:00.000000000Z
---
# Feature Specification: Prove The Hosted Loop

Feature: 007-prove-the-hosted-loop
Created: 2026-11-11

## User Stories

- As the developer, I want the hosted loop proved on the acceptance platform
  before it is built on, so a Darwin-only design is not taken on faith.
- As the operator, I want a window that keeps painting while the transport works,
  so "responsive" is measured and not asserted.
- As the reviewer, I want a written result with numbers, so the ADR can move from
  `proposed` on evidence.

## Scope

A spike on the macOS host `farchanjo@192.168.202.241`: `QSocketNotifier` over the
kqueue fd, the non-blocking turn, and the three capabilities ADR-0020 needs, with
no product code on top.

Out of scope: the store (feature 005), the widgets, the model. The spike drives a
label and a counter, nothing else.

**A spike is throwaway.** It lands under `doc/arch/spikes/hosted-loop/` and is
built by hand on the host; it is not wired into `ctest` until it is repeated as a
feature.

## What Must Be Proved

Each item is a way the design can be wrong, and each has a measurement.

- **FR-001 A `QSocketNotifier` fires on a kqueue fd.** A kqueue with one
  registered read on a socket must wake the Qt loop when the peer writes, with no
  timer involved. Measured: wakes observed, and the latency from write to wake.
- **FR-002 The turn does not block the paint.** With the transport saturating, the
  window must keep repainting. Measured: painted frames per second against an
  idle baseline, over 30 s.
- **FR-003 A modal does not deadlock.** A `QMessageBox` opened from a completion
  must show and dismiss while the transport keeps running.
- **FR-004 Live resize and menu tracking still deliver socket events.** Both are
  nested Cocoa loops. Measured: events observed, none lost, during a 10 s drag
  and a held menu.
- **FR-005 `nextDeadline` drives a `QTimer`.** A timer submitted through the
  engine must fire on the main thread within the timer's own tolerance.
- **FR-006 The turn honours a deadline.** A completion that sleeps 50 ms must not
  run past the turn budget in the next turn. Measured: p99 turn duration and
  overrun count.
- **FR-007 App Nap does not stall the loop.** With the window occluded for 60 s,
  a timer must still fire within an order of magnitude of its period, and
  `NSProcessInfo beginActivity` must be shown to change that.

## Security Requirements

- **Data sensitivity/classification.** The spike moves a loopback byte stream;
  nothing sensitive.
- **Authentication/authorization.** Not applicable — no server, no credential.
- **Input validation.** Not applicable — the peer is a local socket.
- **Cryptography in transit/at rest.** Not applicable — no TLS in the spike.
- **Logging/audit.** The spike prints one line per measurement, to stdout.
- **Error-handling information exposure.** Not applicable — a failed spike prints
  the failure.

## Acceptance Scenarios

Given the spike running on the Darwin host
When  the peer writes to the registered socket
Then  the Qt loop wakes with no timer armed.

Given the spike with the transport saturating
When  the run ends after 30 s
Then  the painted frame rate is at least 80% of the idle baseline.

Given a completion that opens a modal
When  the modal is shown and dismissed
Then  no deadlock and the transport keeps running.

The executable form lives in
`doc/arch/specs/features/prove-the-hosted-loop.feature`.

## Observability

The spike prints, per run: wakes, p50 and p99 turn duration, overruns, painted
frames per second, and any lost event during the drag and the menu. The numbers
are pasted into ADR-0020 when it is accepted.

## Clarifications

- **Why not wire it into `ctest`?** The CI has no Darwin runner. Wiring it would
  make a macOS-only test skip on Linux and prove nothing where it runs.
- **What happens if FR-001 fails?** The fallback is `CFFileDescriptor` in
  `kCFRunLoopCommonModes`, then `dispatch_source` on the main queue. FR-001 names
  the primary only; a failure here is a design change, not a tweak.
