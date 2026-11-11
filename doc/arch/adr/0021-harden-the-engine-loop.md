---
status: proposed
date: 2026-11-11
deciders: [archanjo.com]
consulted: []
informed: []
---

# 0021 — Harden the engine loop before the first window

## Context and Problem Statement

ADR-0020 hosts the engine loop in the Qt main thread. Reviewing that record
turned up defects that exist today, independent of the decision: they are
reachable from the current code and are covered by no test.

The worst one is on Darwin, the acceptance target. `KqueueEngine::arm` registers
with `EV_ONESHOT` and `udata = &op`; `cancelPending` calls `forget` and then
releases the op. `KqueueEngine` never overrides `forget` — only epoll and
io_uring do — so the base no-op leaves the registration armed and the next byte
on that descriptor reaches freed memory. `Ticket::cancel` walks straight into it.

The rest are quieter and just as real: a single resolve slot settles a healthy
connection twice, `fireDueTimers` appends to `tasks_` under a different mutex
than `post`, `cancelPending` walks `ops_` unlocked, `stop()` drops pending ops
without completing them, `stopped_` never returns to false, and the backend name
reports "epoll" when the ring is what runs.

## Decision Drivers

- The hosted loop multiplies every one of these: same thread, more completions,
  more cancellation from the UI.
- A window that inherits them cannot tell a bug from a slow cluster.
- P-13 already runs ASan and the leak pass; these are the defects those passes
  exist to catch and the suite does not reach.
- The fixes are local and add no capability.

## Considered Options

- **Fix them inside ADR-0020's feature.** One initiative, but it mixes a mode
  change with hygiene and makes the ADR reviews unreviewable together.
- **Fix them first, as their own feature.** The hosted loop then lands on a
  base the suite can hold.
- **Leave them and rely on the hosted loop to expose them.** Cheaper now, but
  the first window debugs both at once.

## Decision Outcome

Chosen option: **fix them first, as feature 004.**

Each defect becomes one `FR-NNN` with one scenario. The mode of operation does
not change; `run()` stays the driver and the hosted loop comes after.

### Consequences

- Good: the Darwin UAF closes before any UI can hit it.
- Good: `stop()` completing instead of dropping is a precondition for the
  hosted `shutdown()`, so ADR-0020 gets a smaller surface.
- Good: the suite gains cases for `cancelPending`, `stop` and the resolver —
  today it has none for any of the three.
- Bad: one more feature before the UI, and the UI is what the operator sees.
- Bad: `stop()` completing changes what the leak pass observes, so the leak
  target needs a case.

## More Information

Related: ADR-0006 (the port), ADR-0009 (poll as fallback), ADR-0018 (Linux
order), ADR-0020 (the hosted loop, which depends on FR-005 and FR-006).

Every item was confirmed in the source before this record was written; the line
references live in ADR-0020's "To fix before the first window".
