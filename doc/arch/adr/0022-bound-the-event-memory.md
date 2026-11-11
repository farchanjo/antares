---
status: proposed
date: 2026-11-11
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0022 — Bound the event memory

## Context and Problem Statement

A cluster with many controllers produces many objects and many events. The app
must show them without growing without bound.

Today the decoded shape is a full JSON document per object. `Object` is
`{ json::Value body; ObjectMeta meta; }`, and `ListPage.items` retains one per
row. Measured on this tree, 5 000 pod-shaped objects of 2 691 wire bytes each:

| Representation | Retained | Ratio to wire | CPU |
|---|---|---|---|
| `ListPage.items` as it stands | 96.4 MB | **7.51×** | — |
| `ObjectMeta` alone | 0.9 MB | **0.07×** | 8.8 µs/object |
| Watch frame path | nothing retained | — | 11.9 µs/frame |

The list keeps 107 times what the projection needs, for fields no view reads. A
real cluster makes it worse: `managedFields` is often the largest member of an
object and the UI never shows it.

Two more copies sit on the watch path. `takeFrames` copies each frame out of the
accumulator with `substr` and then `erase`s it, and `decodeWatchEvent` parses the
whole frame and then copies the `object` subtree out of the parsed document, so
two full trees exist for a moment per event.

An identity already exists to key on: `uid`.

## Decision Drivers

- The window must open on a large cluster on a laptop.
- Two views of one resource must not be two copies.
- The parse runs on the main thread once the engine is hosted (ADR-0020), so
  CPU per event is a user-visible cost.
- D-15 forbids an unbounded label set, so the budget is asserted by a test, not
  reported per object.

## Considered Options

- **Keep the full document and cap the count.** Simple, but 7.5× the wire per
  row means the cap has to be small enough to break the product.
- **Store a projection keyed by uid.** Cheapest per row; the UI loses fields it
  does not show.
- **Store the raw wire bytes and decode on demand.** Small, but every view pays
  a parse per access and the parse lands on the main thread.
- **Stream pages through the UI without a store.** No shared identity, so a
  second view is a second copy — the defect this record exists to avoid.

## Decision Outcome

Chosen option: **a projection keyed by uid, in one store per scope.**

One entry per `(cluster, group-version-resource, namespace, uid)`. The entry
holds `uid`, `name`, `namespace`, `resourceVersion`, `generation`, the kind and a
bounded status summary. A label and annotation set is truncated to a fixed
ceiling so a hostile object cannot inflate an entry. `managedFields` is never
decoded.

The bound is 512 bytes per pod-shaped entry, asserted by a test that stores
5 000 objects and fails above 2.5 MB. The ceiling per scope defaults to 20 000
entries with least-recently-updated eviction.

A watch frame is decoded in one pass over the accumulator view: no `substr`, no
`erase` per frame, and no subtree copy. A delta updates the entry for its `uid`
in place; a `DELETED` removes it; a bookmark touches no entry. A 410 marks the
scope stale and the relist replaces it wholesale, so the count never sums.

### Consequences

- Good: the per-object cost drops from 19 528 bytes to under 512, roughly 38×.
- Good: two views share one entry, so the second view is free.
- Good: the per-frame cost stops paying for two trees.
- Bad: the UI cannot show a field the projection dropped without a fresh `get`
  for that object, which is one request on demand rather than a retained copy.
- Bad: the ceiling is a policy, so eviction can remove an entry a view is about
  to read; the view re-fetches it.
- Bad: the entry shape is now a contract that has to move additively (D-16).

## Pros and Cons of the Options

### Keep the full document and cap the count

Cheapest to write and the most expensive to run. At 19.5 KB per object, a 20 000
object scope needs 390 MB. The cap would have to be in the low thousands, which
is below what a real cluster holds.

### Raw bytes with decode on demand

Memory matches the wire, and every access pays a parse. With the loop on the
main thread a scroll through 5 000 rows would be thousands of parses inside the
turn budget. The projection keeps the parse to one per event.

### Stream pages without a store

No identity, so the defect returns: a list in one view and the same scope open
in another are two full sets of trees.

## More Information

Related: ADR-0017 (the API client, which owns the decode), ADR-0020 (the hosted
loop, which puts the parse on the main thread), ADR-0021 (the engine hardening
that precedes it).

Threat: TM-17 in `doc/arch/threat-model/threat-model.md` records the object-count
exhaustion this bounds.
