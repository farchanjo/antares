---
status: proposed
date: 2026-11-12
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0027 — One entry point owns the order

## Context and Problem Statement

Nine features describe components and none describes assembling them. Without a
stated order, every component guesses who outlives whom, and the guesses
disagree — the client's destructor already guessed wrong once (FR-026 of 002).

Two facts fix most of the answer. Every component posts to the engine, so the
engine outlives all of them. And the loop is hosted on the main thread, so
nothing processes it after `exec()` returns — which makes a passive shutdown a
deadlock.

## Decision Drivers

- One place must own the order, or each component owns a guess.
- A signal may only run async-signal-safe code.
- A failure half way through construction may not leak the half.
- The engine outlives every poster; that is not negotiable.

## Considered Options

- **Components own their own lifetime.** Each registers a shutdown hook; the order
  is emergent and untestable.
- **One entry point with a stated order.** A single function builds, a single
  path tears down.
- **A framework container.** A DI container resolves and orders; the project has
  no container today.

## Decision Outcome

Chosen option: **one entry point with a stated order.**

Construction is one function, in dependency order, and every `QObject` takes its
parent so the tree owns it. The engine is the only `unique_ptr`, because it is
not a `QObject`. A failure unwinds what was already built before exiting.

The loop is attached, not run: a `QSocketNotifier` over `waitHandle()` drives
`runOnce()` with `nextDeadline()`.

Destruction is the reverse and runs after the drain, with the engine last.
`SIGTERM` and `SIGINT` reach the same path by writing one byte to a self-pipe;
`SIGPIPE` is ignored so a closed peer is an error, not a death.

A missing current context opens an empty window naming the file, rather than a
dialog — a dialog before `exec()` needs a nested loop for the same information.

### Consequences

- Good: one function answers "who owns what" and it can be read in a minute.
- Good: the shutdown order is stated where it can be reviewed, not inferred from
  nine destructors.
- Good: a new component has an obvious slot.
- Bad: the entry point grows with the product, and it is the file most likely to
  become a pile.
- Bad: the order is now a contract, so changing it is an ADR-level act.

## Pros and Cons of the Options

### Components own their own lifetime

No central file, and the order is emergent: two components that must not be freed
before each other cannot express it, only discover it in a crash.

### A framework container

Real ordering and real lifetimes, at the cost of a dependency and of hiding the
one function that matters most during a shutdown bug.

## More Information

Related: ADR-0020 (the hosted loop), ADR-0006 (the engine outlives its posters),
ADR-0022 (the store the model reads), ADR-0026 (P-12, which the tree uses).

Features: 002 FR-026 and FR-027 state the drain and the two shutdown sources.
