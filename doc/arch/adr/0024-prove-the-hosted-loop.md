---
status: proposed
date: 2026-11-11
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0024 — Prove the hosted loop before building on it

## Context and Problem Statement

ADR-0020 decides to host the engine loop in the Qt main thread and rests on one
unverified assumption: that a `QSocketNotifier` over a kqueue fd wakes the Qt loop
the way a socket notifier over a plain fd does. Nobody has run it. The record
says `proposed` for exactly that reason.

Everything downstream — the store, the model, the window — is built on a loop that
may not work on the acceptance platform. A failure found after the store exists
costs the store.

Separately, the CI has no Darwin runner, so a macOS-only proof cannot be a gate;
it has to be a spike with a written result.

## Decision Drivers

- P-05 makes Darwin the acceptance platform; Linux proves nothing about kqueue.
- A design taken on faith is one the reviewer cannot check.
- The spike is cheaper than the feature it protects.
- A spike that is not written down is a rumour.

## Considered Options

- **Build the feature and find out.** The failure surfaces after the store, the
  model and the widgets exist.
- **Spike on the host, record the numbers, then decide.** Throwaway code, one
  afternoon, an ADR that moves on evidence.
- **Skip and trust the platform.** `QSocketNotifier` wrapping `CFSocket` is
  documented to work over any pollable fd; trust is arguably enough.

## Decision Outcome

Chosen option: **spike first, on the host, with a written result.**

Seven measurements, each a way the design can be wrong: the notifier fires, the
paint survives a saturated transport, a modal does not deadlock, nested Cocoa
loops still deliver, `nextDeadline` drives a timer, the turn honours a deadline,
and App Nap does not stall the loop.

The spike lives under `doc/arch/spikes/hosted-loop/`, is built by hand on
`farchanjo@192.168.202.241`, and is not wired into `ctest`. The numbers go into
ADR-0020, which moves from `proposed` on them.

A failure at FR-001 is a design change, not a tweak: the fallback is
`CFFileDescriptor` in `kCFRunLoopCommonModes`, then `dispatch_source` on the main
queue.

### Consequences

- Good: ADR-0020 stops resting on an assumption.
- Good: the seven measurements become the acceptance criterion for the hosted
  mode, so a later regression has a baseline.
- Bad: a spike is code that rots; it is not built by CI, so it will not compile
  in a year without attention.
- Bad: it needs the host, so it cannot be done from the workstation alone.

## Pros and Cons of the Options

### Build the feature and find out

Cheapest to start and the most expensive to be wrong: the store, the model and the
widgets all assume the loop works.

### Skip and trust the platform

Defensible — `QSocketNotifier` over `CFSocket` is documented for any pollable fd —
but it leaves ADR-0020 `proposed` forever and the reviewer with nothing to read.

## More Information

Related: ADR-0020 (the decision this proves), ADR-0006 (the engine port),
ADR-0009 (poll as fallback).

The spike is throwaway and must not be promoted by copy-paste into the feature;
the feature re-derives the glue with tests.
