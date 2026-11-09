---
status: accepted
date: 2026-11-09
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0014 — Offline by default is advisory

## Context and Problem Statement

D-13 says the core loop validates, phases and guards with the network
unplugged, and that the first network path is opt-in and degrades. The rule is
block, but its enforcement list names `core.io-direct`, which belongs to D-03
and only asserts where I/O lives — it never proves a run works offline.

`validate` reports the gap: a block doctrine with no automated check of its own.

## Decision Drivers

- A block rule that nothing checks teaches the reader to distrust block rules.
- The constraint is real: no production path dials out unless a caller asks.
- A proper check needs a build mode and a sandbox, which is a feature.

## Considered Options

- **Point D-13 at a new offline-run rule.** Right end state, wrong moment: the
  rule does not exist yet.
- **Delete D-13.** Loses the constraint, which the engine already honours.
- **Downgrade to advisory and record it.** Keeps the rule, stops claiming a gate.

## Decision Outcome

Chosen option: **downgrade D-13 to `advisory` and record the amendment here.**

The rule stays in force as a design constraint. Re-promoting it to block
requires the offline-run rule first.

### Consequences

- Good: the corpus stops reporting an unenforced block rule.
- Good: the constraint is still written down and reviewable.
- Bad: nothing fails a change that reaches the network from the core loop.

## Pros and Cons of the Options

### New offline-run rule

Costs a build mode plus a sandbox; buys a real gate. Blocked on scheduling, not
on design.

### Delete D-13

Cheapest, but the next reader loses the hint that the core is meant to run
without a network.

## More Information

Related: D-03 (dependencies point inward), `core.io-direct`.
