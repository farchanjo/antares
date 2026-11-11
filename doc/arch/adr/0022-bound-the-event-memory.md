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
| `ListPage.items` as it stands | 96.4 MiB | **7.51×** | — |
| `ObjectMeta` alone, flat vector | 0.9 MiB | 0.07× | 8.8 µs/object |
| Watch frame path | nothing retained | — | 11.9 µs/frame |

96.4 MiB over 5 000 objects is 20 217 bytes per object — 107 times what the
projection needs, for fields no view reads. A real cluster is worse:
`managedFields` is often an object's largest member and the UI never shows it.

Two copies sit on the watch path. `takeFrames` copies each frame out of the
accumulator with `substr` into a vector of strings (`watch_event.cpp:124`), and
`decodeWatchEvent` copy-assigns the `object` subtree out of the parsed document
(`:91`), because `member` returns a `const &` (`value.hpp:49`). Two full trees
exist for a moment per event. The accumulator compacts once per call (`:128`),
not once per frame — the copies are the defect, not the compaction.

At 7.51× the 64 MiB `maxBodyBytes` ceiling (`limits.hpp:16`), one unpaginated
list builds about 480 MiB of transient tree.

An identity already exists to key on: `uid`.

## Decision Drivers

- The window must open on a large cluster on a laptop.
- Two views of one resource must not be two copies.
- The parse lands on the main thread once the loop is hosted (ADR-0020), so CPU
  per event is user-visible.
- A laptop waking after five minutes gets a 410 on every scope, so the relist
  path is the common path.
- D-15 forbids an unbounded label set, so the budget is asserted by a test.

## Considered Options

- **Keep the full document and cap the count.** At 20 217 bytes per object a
  50 000-object scope needs 964 MiB.
- **Store a projection keyed by uid.** Cheapest per row; the UI loses fields it
  does not render.
- **Store raw wire bytes and decode on demand.** Memory matches the wire, but a
  scroll becomes thousands of parses inside the turn budget.
- **`Accept: as=Table;g=meta.k8s.io;v=v1`.** The server renders the columns and
  the client decodes no body — cheaper than every option above.
- **Stream pages without a store.** No identity, so a second view is a second
  copy: the defect this record exists to avoid.

## Decision Outcome

Chosen option: **a projection keyed by uid, in one store per scope.**

One entry per uid inside one scope `(cluster, group-version-resource, namespace)`.
The entry holds `uid`, `name`, `namespace`, `resourceVersion`, `generation`,
`creationTimestamp`, a label set truncated at 16 entries or 256 bytes, and a
bounded status summary. Annotations and `managedFields` are never decoded,
because an annotation can carry a `Secret`'s data.

The fixture pod costs at most 512 bytes and any admitted entry at most 1 024,
counted through the allocator. A scope admits 50 000 entries and the store
100 000 across 32 scopes, so the store stays under 100 MiB. A full scope
refuses, counts and says so; nothing is evicted.

Admission rather than eviction is the load-bearing choice: evicting inside
`data() const` is a mutation on the paint path, a pin puts UI state in the
application layer, and eviction shifts `rowCount` under the scrollbar. Admission
keeps the row set stable.

A watch frame is parsed once from a view into the accumulator, the projection is
read without copying the `object` subtree, and the accumulator compacts once per
chunk. A delta updates its uid in place; a `DELETED` removes it; a bookmark
touches no entry. A 410 marks the scope stale and a paginated relist reconciles
it by epoch, so the count never sums and only one copy of the scope exists. Each
batch is announced as row ranges a list model applies directly, on the model's
thread.

### Consequences

- Good: the per-object cost drops from 20 217 bytes to under 512 for the
  fixture, about 39×.
- Good: two views share one entry, so the second view is free.
- Good: the relist path, which is the common one, stops resetting the model.
- Bad: a scope past its ceiling goes partial; the store publishes `truncated`
  and the refused count, and the view has to show them.
- Bad: the on-demand `get` cannot address a name with `:` yet, because
  `ApiPath::named` requires a DNS-1123 subdomain (`api_path.cpp:131-135`). That
  fix belongs to feature 003.
- Bad: until ADR-0020 is accepted, every batch crosses threads once.
- Bad: the entry shape moves additively (D-16), so the field set is expensive to
  change once shipped.

## Pros and Cons of the Options

### Keep the full document and cap the count

Cheapest to write, most expensive to run: 964 MiB for a 50 000-object scope,
below what a real cluster holds.

### Raw bytes with decode on demand

Memory matches the wire and every access pays a parse. With the loop on the main
thread a scroll through 5 000 rows is thousands of parses inside the turn budget.

### `as=Table`

Cheaper than the chosen option and the next lever: the server computes the
columns per kind, including CRD `additionalPrinterColumns`, and the client
decodes no body. Deferred, not rejected — it needs one negotiated shape per kind
and the CRD path is untested. It also gives no identity or sharing by itself,
which the store still has to provide.

### Stream pages without a store

No identity, so the defect returns: a list in one view and the same scope in
another are two full sets of trees.

## More Information

Related: ADR-0017 (the API client, which owns the decode), ADR-0020 (the hosted
loop, which puts the parse on the main thread), ADR-0021 (the engine hardening
that precedes it).

Threat: TM-17 in `doc/arch/threat-model/threat-model.md` records the
object-count exhaustion this bounds, with the global entry ceiling that makes it
true.
