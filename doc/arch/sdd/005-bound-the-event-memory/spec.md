---
id: 01a1cbf4-9b31-7c44-a0d3-5e8f21b7c9da
number: 005
slug: bound-the-event-memory
status: analyzed
created_at: 2026-11-11T19:05:00.000000000Z
---
# Feature Specification: Bound The Event Memory

Feature: 005-bound-the-event-memory
Created: 2026-11-11

## User Stories

- As the operator, I want a cluster with fifty thousand objects to open without
  the app eating a gigabyte, so the window stays usable on a laptop.
- As the operator, I want one row per object even when two views show the same
  resource, so the second view is not a second copy.
- As the developer, I want the memory cost per object to be a number in the
  corpus, so a regression is a failing test and not a surprise.

## Scope

The representation a decoded Kubernetes object takes in memory, and the store
that holds it between updates. No transport change and no UI.

Out of scope: the wire format, the watch framing itself, the UI, and any
informer policy (relist cadence, bookmarks) beyond what the store needs.

## Measured Baseline

Measured on this tree before any change, 5 000 synthetic pods of 2 691 wire
bytes each (12.8 MB total):

| Representation | Retained | Ratio to wire | CPU |
|---|---|---|---|
| `ListPage.items` today (`json::Value` body + `ObjectMeta`) | 96.4 MB | **7.51×** | — |
| `ObjectMeta` only (the projection) | 0.9 MB | **0.07×** | 8.8 µs/object |
| Watch frame path | 0.1 MB peak, nothing retained | — | **11.9 µs/frame** |

The gap between the first two rows is the defect: the list retains 107 times
what the projection retains, for data no view reads. A real cluster with
`managedFields` on every object widens it.

## Functional Requirements

### FR-001 One representation per uid

A decoded object is stored once per `(cluster, group-version-resource,
namespace, uid)`. Two views reading the same resource share that one entry, and
a second `list` of the same scope replaces the entry instead of adding one. The
store reports the number of entries it holds.

### FR-002 The stored shape is a projection

The store keeps the fields a view reads and nothing else: `uid`, `name`,
`namespace`, `resourceVersion`, `generation`, the kind, and a bounded summary of
status. The full document is not retained, and `managedFields` is never decoded
into the stored shape.

### FR-003 The per-object cost is bounded

A stored entry costs no more than 512 bytes for a pod-shaped object, against the
measured 19 528 bytes per object the list path takes today (96.4 MB / 5 000).
The bound is asserted by a test that stores 5 000 objects and fails above
2.5 MB.

### FR-004 Retention is bounded by scope, not by history

The store evicts least-recently-updated entries per scope when the scope exceeds
its ceiling, and the ceiling is a configured value with a default of 20 000
entries. A relist of a scope does not double it: entries absent from the relist
are removed.

### FR-005 A watch frame is parsed once

A frame is decoded into the projection in a single pass over the buffer view. The
frame is not copied out of the accumulator and the payload subtree is not copied
out of the parsed document. After the change, decoding 5 000 frames does not
exceed 1.5 times the cost of one parse per frame.

### FR-006 A delta reaches the store by uid, without a second copy

An `ADDED` or `MODIFIED` event updates the entry for its `uid` in place; a
`DELETED` removes it; a bookmark advances the version and touches no entry. The
store never holds two entries for the same `uid`.

### FR-007 The store survives a 410 without duplicating

An expired watch marks the scope stale and a relist replaces it wholesale. After
the relist the entry count for the scope equals the number of objects the server
returned, never the sum of both lists.

## Security Requirements

- **Data sensitivity/classification.** The store holds object metadata: names,
  namespaces, uids, labels and a status summary. Secrets are excluded by FR-002 —
  a `Secret`'s `data` is never decoded into the projection.
- **Authentication/authorization.** No new surface. The store holds what the
  credential was already allowed to list.
- **Input validation.** The untrusted input is the object count and the field
  sizes. FR-003 and FR-004 bound the first; label and annotation counts are
  truncated to a fixed ceiling, so a hostile object cannot inflate an entry.
- **Cryptography in transit/at rest.** Not applicable — nothing here is
  encrypted or persisted; the store is memory only.
- **Logging/audit.** Eviction and staleness are logged at `warn` with the scope
  and the count. No object name is logged above `debug`.
- **Error-handling information exposure.** A decode failure reports the frame
  offset and the reason, never the payload.

## Acceptance Scenarios

Given a scope with 5 000 objects
When  the list is stored
Then  the store holds 5 000 entries and less than 2.5 MB.

Given two views of the same scope
When  both resolve an object by uid
Then  both receive the same entry.

Given an object already stored
When  a MODIFIED event for its uid arrives
Then  the entry is updated in place and the count does not change.

The executable form lives in
`doc/arch/specs/features/bound-the-event-memory.feature`.

## Observability

Two bounded-label metrics: `store.entries{scope_kind}` where `scope_kind` is
`cluster|namespace`, and `store.evictions{scope_kind}`. Both cardinalities are
fixed at two. Entry bytes are asserted by the test rather than reported, because
a per-object label would be unbounded (D-15).

## Clarifications
