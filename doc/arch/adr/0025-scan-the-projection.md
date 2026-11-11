---
status: proposed
date: 2026-11-11
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0025 — Scan the projection instead of parsing the document

## Context and Problem Statement

Feature 005 stores a projection but parses the whole document first.
`decodeEntry` runs after `Value::parse`, which builds every member —
`managedFields` and `annotations` included — and the caller then drops them.

Measured on a 4 431 byte pod frame, 39% of it `managedFields`: `Value::parse`
takes 15.8 µs and 193 allocations. A scanner that reads only the projection's
fields takes 1.97 µs.

The projection already knows which fields it reads. Building the rest is work the
feature does and throws away.

## Decision Drivers

- Event bursts land on the main thread once the loop is hosted (ADR-0020).
- 193 allocations per frame is allocator pressure the store never keeps.
- `as=Table` is local to the server's shape and 1.7×; a CRD gets nothing from it.
- A third parser is risk, so the saving must be measured and the contract tested.

## Considered Options

- **Keep `parse` then project.** No new parser. 15.8 µs and 193 allocations per
  frame for data no view reads.
- **`as=Table` with `includeObject=Metadata`.** 1.7× and no shape of our own, but
  `managedFields` stays in the row and `None` drops the `uid` the store keys on.
- **A local selective scan.** 8×, any kind, no server negotiation, at the cost of
  a third parser to keep correct.

## Decision Outcome

Chosen option: **a local selective scan.**

The scanner walks the document, keeps the projection's fields and skips the rest
without allocating. Skipping still validates — braces, brackets, quotes and
escapes — so a malformed document fails at its offset rather than producing a
truncated entry. A kept string is unescaped.

The saving is asserted, not assumed: the case reports both paths and fails under
4×, the conservative half of the 8× measured, so a slow machine does not fail the
build.

The entry type is shared with feature 005, so a document shape the scanner cannot
match falls back to `Value::parse` and the scanner is fixed after. `KubeStats`
gains `scanFailures`, so a document the parser accepts and the scanner refuses is
visible.

### Consequences

- Good: 8× on the hot path and roughly an order of magnitude fewer allocations.
- Good: works for a CRD, which `as=Table` would need a shape for.
- Good: `Secret.data` stays out of memory by construction, not by discipline.
- Bad: a third parser, and the two must agree — FR-001 is the guard.
- Bad: escapes and validation are easy to get subtly wrong, so the fixtures carry
  the awkward cases.

## Pros and Cons of the Options

### Keep `parse` then project

Nothing new to keep correct, and the full document is available if a later view
wants a field. Costs 193 allocations per frame for data already dropped.

### `as=Table` with `includeObject=Metadata`

Cheap to adopt and server-side, at 1.7×. Keeps `managedFields`, needs one
negotiated shape per kind, and the CRD path is untested.

## More Information

Related: ADR-0022 (the store and the projection), ADR-0020 (the hosted loop, which
puts the burst on the main thread), ADR-0017 (the client that owns the decode).

Measurements were taken with clang 18, libc++, `-O2`, against the real
`value.cpp`. A `-O0` build is 13× slower and is not meaningful here.
