# Implementation Plan: Bound The Event Memory

## Overview

The decoded shape costs 7.51× the wire and no view reads most of it. The plan
replaces a retained document with a projection keyed by `uid`, in one store per
scope, removes the two copies the watch path takes per frame, and makes the
relist reconcile instead of reset.

Target: 20 217 bytes per object today, under 512 typical after.

## Technical Approach

**Layers.** Projection and store are `domain` values plus an `application`
service; nothing here opens a socket.

**FR-001, FR-006 — identity.** `#ObjectKey` is `(scope, uid)` with `scope` a
`(cluster, gvr, namespace)` triple hoisted out of the entry. The store owns
`std::unordered_map<#ObjectKey, #Entry>` per scope and a monotonic touch tick
that is **not** in `#Entry`, so D-16 does not freeze eviction state. `upsert`
replaces in place; `erase` removes; a bookmark touches nothing. An unknown uid on
`MODIFIED`/`DELETED` is counted, not an error — it is what an eviction looks
like from the stream.

**FR-002, FR-003 — the projection.** `decodeEntry` reads only its members and
returns `#Entry`; the document is consumed and dropped inside the function. The
uid is stored once, `gvr` and `namespace` live in the scope, labels are interned
per scope. `managedFields` is never read. The annotation allowlist excludes
`last-applied-configuration`.

`json::Value::member` returns a `const &` (`value.hpp:49`), so the decoder takes
`const json::Value &` and copies only the small fields; no moving accessor is
needed, which removes a risk from the earlier draft.

**FR-004, FR-009, FR-010 — retention.** Per-scope ceiling, eviction by
least-recently-touched, a refcount pin for the selected row and the rendered
window, a `partial` mark on a displayed scope above its ceiling, and a global
ceiling on open scopes with LRU close-and-reopen.

**FR-005 — one parse per frame.** `takeFrames` becomes a cursor over the
accumulator reporting consumed bytes; the accumulator compacts once per chunk
instead of once per call (`watch_event.cpp:128`). `decodeWatchEvent` gains a `string_view` overload
returning the projection directly, so neither the `substr` nor the subtree copy
happens. Asserted with integer counters: parses equal frames, allocations at or
under three per frame, compactions at most one per chunk.

**FR-007 — reconciliation.** A 410 marks the scope stale. The relist accumulates
a seen-set over the pages and reconciles at the end: absent uids removed, seen
ones inserted or updated. The model keeps identity, so selection and scroll
survive. `stale` and `partial` are scope state, carried in the CUE `#Scope`.

**FR-008 — the delta.** The store publishes `{key, op, changedFieldMask}`; the
mask maps to model roles. An unrendered change emits nothing; an ordering-key
change emits a move.

### Threading

While ADR-0020 is `proposed`, `WatchHandler::onEvent` runs on the engine thread
(`kube_client.hpp:68-70`) and the model lives on the GUI thread. The store is
therefore either locked or the delta is posted; the plan takes **post to the
GUI**, because the batch already has to be queued for FR-008 and a lock would be
held across model notification. FR-009 makes the confinement a requirement, not
an implementation detail.

### Test strategy

One conformance case per FR. The memory bound counts map node, key, bucket array
and index — not `sizeof(Entry)` — from a counting allocator, so it is
deterministic (P-03). Parse and allocation counts use integer counters, never a
clock. FR-009 and FR-010 are asserted on the eviction order directly.

### Risks

- Dropping a rendered field forces a `get` per row; the field list in FR-002 is
  the contract, and it moves additively.
- Interned labels need a scope-owned pool with its own lifetime; a dangling
  intern is a use-after-free, so the pool dies with the scope.
- The accumulator still grows until a `\n` arrives (`http_kube_client.cpp:308`);
  whether `maxBodyBytes` covers a stream is unconfirmed and must be checked in
  T008.
- `ListPage.items` changing type is not additive (D-11); `get` keeps returning
  `Object` with a body, and the spec says so.
- `ApiPath::named` requires a DNS-1123 subdomain (`api_path.cpp:131-135`), so a
  name with `:` cannot be fetched on demand until feature 003 relaxes it.

## Constitution

- **D-05 — primitives wrapped.** `#NamespaceName` and `#ObjectName` are distinct
  because the server's rules differ; the budget shapes live in their own file to
  stay under the definition ceiling.
- **D-15 — bounded labels.** Three metrics, two of them with a two-value label;
  per-object bytes are asserted, never labelled.
- **D-16 — versioned contract.** `#Entry` moves additively; the eviction tick
  stays out so it is not frozen.
- **D-11 — additive surface.** `get` keeps `Object`; only the store holds
  projections.
- **P-03 — determinism.** The budget is counted, not measured by RSS.
- **D-02 — dogfood.** Every FR has a case in the suite the project runs.

## Companion Artifacts

- `contracts/` — none; the store is internal.
- `data-model.md` — not needed; `doc/arch/schemas/bound-the-event-memory.cue` is
  the model.
