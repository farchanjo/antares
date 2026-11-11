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
- As the operator, I want two views of the same resource to share one copy, so
  the second view is not a second set of documents.
- As the operator, I want a scope that hit its ceiling to say so, so the list
  does not silently thin out under me while I scroll.
- As the developer, I want the memory cost per object to be a number in the
  corpus, so a regression is a failing test and not a surprise.

## Scope

The representation a decoded Kubernetes object takes in memory, the store that
holds it between updates, and the batch a view applies.

**Scope** is a `(cluster, group-version-resource, namespace)` triple. A
cluster-scoped list of `pods` and a `default`-scoped one are two scopes, and a
view of the narrow one is served by filtering the wide one **when both are
open**; otherwise the narrow view opens its own. Both cases are stated in FR-001.

Out of scope: the wire format, the watch framing itself, the UI widgets, and any
informer policy (relist cadence, bookmarks) beyond what the store needs.

## Measured Baseline

Measured on this tree before any change, 5 000 synthetic pods of 2 691 wire
bytes each (12.8 MiB total). Units are MiB throughout.

| Representation | Retained | Ratio to wire | CPU |
|---|---|---|---|
| `ListPage.items` today (`json::Value` body + `ObjectMeta`) | 96.4 MiB | **7.51×** | — |
| `ObjectMeta` only, in a flat vector | 0.9 MiB | 0.07× | 8.8 µs/object |
| Watch frame path | nothing retained (0.1 MiB peak) | — | **11.9 µs/frame** |

96.4 MiB over 5 000 objects is **20 217 bytes per object**. The flat-vector
figure understates the store: it has no map node, no key and no index.

At 7.51× the 64 MiB `maxBodyBytes` ceiling (`limits.hpp:16`), one unpaginated
list builds about 480 MiB of transient tree. That is why FR-007 paginates.

## Functional Requirements

### FR-001 One entry per object, keyed by scope and uid

The store holds one entry per `(cluster, group-version-resource, namespace,
uid)`. Two views resolving the same key receive the **same stored entry** — the
same object, not an equal copy. When a cluster-scoped view and a namespaced view
of the same resource are both open, the namespaced one is a filter over the
cluster scope and does not open a second store; a namespaced view opened alone
opens its own. `get` continues to return the full document; only the store holds
projections.

### FR-002 The stored shape is a projection

The store keeps the fields a view reads and nothing else: `uid`, `name`,
`namespace`, `resourceVersion`, `generation`, `creationTimestamp`, the kind, a
truncated label set, and a bounded summary of status. The full document is not
retained. `managedFields`, `annotations`, and a `Secret`'s `data` and
`stringData` are never decoded into the stored shape.

### FR-003 The per-object cost is bounded

Every byte the store allocates counts: the entry, its strings, its labels, its
index node and its bucket share.

A stored entry for the fixture `tests/k8s/fixtures/pod.json` (2 691 wire bytes,
three labels, a 33-character name) costs at most **512 bytes**, against the
measured 20 217 bytes per object the list path takes today (96.4 MiB / 5 000).

Any admitted entry costs at most **1 024 bytes**: labels past their ceiling are
truncated, and an object whose name exceeds 253 bytes or whose namespace exceeds
63 is refused.

A test stores 5 000 fixture pods through a counting allocator and fails above
2 560 000 bytes, and stores one worst-case object and fails above 1 024 bytes.

### FR-004 Retention is bounded by admission, not by eviction

A scope admits at most `maxEntries` entries (default 50 000) and the store at
most `maxTotalEntries` across at most `maxScopes` open scopes (defaults 100 000
and 32).

An object that would exceed a ceiling is **refused and counted**, and its scope
is published as `truncated`; no stored entry is evicted to make room. An entry
leaves the store only on a `DELETED` or a reconciliation sweep (FR-007).

At the defaults the store retains at most
`100 000 × 1 024 + 32 × 65 536 = 104 497 152` bytes, inside the declared budget
of 104 857 600 bytes (100 MiB).

### FR-005 A watch frame is parsed once

Each non-blank frame is parsed exactly once, from a view into the accumulator,
and the projection is read from the parsed document without copying the `object`
subtree; the overload returns the projection, never a `json::Value`. The
accumulator compacts at most once per received chunk.

`KubeStats` gains `framesParsed` and `bufferCompactions`; over 5 000 frames
delivered in C chunks the test asserts `framesParsed == 5 000` and
`bufferCompactions <= C`.

### FR-006 A delta reaches the store by key

`ADDED` upserts, `MODIFIED` updates in place, `DELETED` removes, a bookmark
touches no entry. A `MODIFIED` or `DELETED` for an unknown uid is not an error:
it is what a refusal looks like from the stream, and it is counted. An `ADDED`
for a known uid is treated as an update.

### FR-007 A relist reconciles the scope

An expired watch (410) marks the scope stale and its rows stay visible. The
relist is paginated with `limit` 500 and opens a new epoch: every object of
every page is upserted and marked with it, so an unchanged row emits nothing and
a changed one emits a change (FR-008).

After the final page, the rows not marked with the epoch are removed in one
batch. The entry count then equals the number of objects the server returned,
capped by FR-004, never the sum of both lists. A relist that fails before its
final page sweeps nothing and leaves the scope stale. Only one copy of the scope
exists at any time.

### FR-008 The store announces each batch in row terms

The store applies events in batches, one per received chunk or relist page, and
publishes one `ChangeSet` per batch. Rows keep arrival order and new rows are
appended.

A `ChangeSet` lists removed row ranges in descending order against the rows
before the batch, inserted ranges against the rows after it, and changed rows
with a field mask over `resourceVersion`, `generation`, `labels` and `status`.
Several events for one uid in a batch yield one change.

Replaying a `ChangeSet` onto a mirror of the rows reproduces the store's rows
exactly, so a `QAbstractListModel` adapter maps it one-to-one onto
`beginRemoveRows`, `beginInsertRows` and `dataChanged` with roles derived from
the mask. The store does not depend on Qt.

### FR-009 The store lives on one thread

The store and every `ChangeSet` it emits are confined to the thread the list
model runs on. While the engine runs on its own thread, a decoded batch crosses
to the main thread as a value and is applied there; once ADR-0020 hosts the
loop, it is applied in the same turn.

## Security Requirements

- **Data sensitivity/classification.** The store holds object metadata: uid,
  name, namespace, resource version, generation, creation time, a truncated
  label set and a status summary. Annotations are never decoded into the
  projection: `kubectl.kubernetes.io/last-applied-configuration` on a `Secret`
  carries its `data`, and any annotation may carry a token. A `Secret`'s `data`
  and `stringData` are never decoded into the projection; the parsed frame holds
  them for one decode and is released, not wiped — a residual risk, recorded.
- **Authentication/authorization.** No new surface. The store holds what the
  credential was already allowed to list.
- **Input validation.** The untrusted input is the object count and the field
  sizes. FR-004 bounds the count. FR-003 bounds each entry: labels truncate at 16
  entries or 256 bytes, whichever comes first, and the entry records the
  truncation; an oversized name or namespace refuses the object.
- **Cryptography in transit/at rest.** Not applicable — memory only.
- **Logging/audit.** Refusal, truncation and staleness are logged at `warn` with
  the scope and the count. No object name is logged above `debug`.
- **Error-handling information exposure.** A decode failure reports the frame
  offset and the reason, never the payload.

## Acceptance Scenarios

Given a scope with 5 000 fixture pods
When  the list is stored
Then  the store allocates at most 2 560 000 bytes.

Given a scope at its ceiling
When  another object arrives
Then  it is refused, counted, and the scope is published as truncated.

Given a stored object
When  a MODIFIED touches only an unrendered field
Then  no row is repainted.

The executable form lives in
`doc/arch/specs/features/bound-the-event-memory.feature`.

## Observability

Two bounded-label metrics: `store.entries{scope_kind}` and
`store.refused{scope_kind}`, where `scope_kind` is `cluster|namespace`. Both
cardinalities are fixed at two. Per-object bytes are asserted by the test rather
than labelled, because a per-object label would be unbounded (D-15).

## Clarifications

- **Why admission instead of eviction?** Evicting inside `data() const` is a
  mutation on the paint path, a pin puts UI state in the application layer, and
  eviction shifts `rowCount` under the scrollbar. Admission keeps the row set
  stable: it grows until a ceiling and then says so.
- **Why no annotations at all?** A deny-list of one key is fragile — any
  annotation can carry a token — and a key-only set renders nothing.
- **Why not `as=Table`?** Cheaper still, but it needs one negotiated shape per
  kind and the CRD path is untested. The ADR records it as the next lever.
- **Why store labels whole?** Truncation breaks selectors: `Service→Pods` and
  `pod-template-hash` report a false "no match" when the key or value is cut.
- **Why is the tick not in the contract?** `updatedAtTick` is store bookkeeping;
  a view never reads it.
