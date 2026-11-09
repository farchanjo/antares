---
status: accepted
date: 2026-11-09
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0009 — kqueue is the engine, poll is the fallback

## Context and Problem Statement

ADR 0006 chose a port and named kqueue the macOS production backend, with
`poll` as reference. "Reference" left room for reading `poll` as an equal
option, which invites a call site to choose and the engine to drift.

The requirement is the opposite: kqueue is what runs. The abstraction exists so
the rest of the system never learns that, and so a swap never reaches a caller.

## Decision Drivers

- kqueue is O(1) per registration; `poll` is O(n) per sweep.
- The kernel can refuse a queue. A refusal must not abort the application.
- A single owner of the choice keeps call sites free of `#ifdef`.
- A fallback that never runs rots; it needs a way to be exercised.

## Considered Options

- **kqueue forced, `poll` on refusal or override** — one default, one escape.
- **Availability probing at build time only** — no runtime refusal path.
- **Caller chooses per open** — decision leaks upward.

## Decision Outcome

Chosen option: **kqueue forced, `poll` reachable only by kernel refusal or
explicit override.**

`makeEngine()` builds `KqueueEngine` on Apple. The constructor probes the
kernel; `ready()` reports whether the queue exists. A refusal falls through to
`PollEngine` without unwinding the call site, which already holds the port.

`ANTARES_IO_BACKEND=poll` forces the fallback for diagnosis and for a suite run
that must prove the fallback still works. `selectedBackendName()` reports the
choice without building an engine, so the name is loggable at startup.

Both backends derive from `EngineBase`, which owns the op store, the wakeup
pipe, the posted queue and the loop. A backend supplies only how it waits. That
keeps the immediate file strategy identical in both and the port unchanged.

### Consequences

- Good: kqueue is the path in production, not a preference.
- Good: a refused queue degrades instead of aborting.
- Good: the override makes the fallback reachable by the suite.
- Bad: two backends compile into the binary, so the fallback is dead weight
  until exercised.
- Bad: `ready()` is a second failure channel beside `std::expected`.
- Bad: `EngineBase` is a base class, so a backend change touches shared
  machinery.

### Confirmation

`makeEngine()` names kqueue first on Apple, the suite runs both backends through
`ANTARES_IO_BACKEND`, and no call site outside the factory names one.
