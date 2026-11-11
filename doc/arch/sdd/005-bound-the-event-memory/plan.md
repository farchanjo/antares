# Implementation Plan: Bound The Event Memory

## Overview

The decoded shape costs 7.51× the wire and no view reads most of it. The plan
replaces a retained document with a projection keyed by `uid`, in one store per
scope, removes the two copies the watch path takes per frame, and makes the
relist reconcile instead of reset.

Target: 20 217 bytes per object today, under 512 typical after.

### Measured, on a 4 431 byte pod frame (39% `managedFields`)

| Path | Time | Allocations |
|---|---|---|
| `Value::parse` alone | 15.8 µs | 193 |
| Parse + subtree copy | 19.6 µs | +86 |
| Full cycle, copy and free | — | — |
| `as=Table`, `includeObject=Metadata` | 9.4 µs | — |
| Selective scan | 1.97 µs | — |

The copy costs about 20%. `as=Table` is 1.7×, not an order of magnitude, because
`managedFields` stays in the metadata row. The order of magnitude is the local
scan, at 8×, and it is feature 008. A `-O0` build is 13× slower, so only the
optimized build is meaningful.

## Technical Approach

**Layers.** Projection and store are `domain` values plus an `application`
service; nothing here opens a socket.

**FR-001, FR-006 — identity.** `#ObjectKey` is `(scope, uid)` with `scope` a
`(cluster, gvr, namespace)` triple hoisted out of the entry. The store owns
`std::unordered_map<#ObjectKey, #Entry>` per scope and a monotonic touch tick
that is **not** in `#Entry`, so D-16 does not freeze store bookkeeping. `upsert`
replaces in place; `erase` removes; a bookmark touches nothing. An unknown uid on
`MODIFIED`/`DELETED` is counted, not an error — it is what a refusal looks like
from the stream.

**FR-002, FR-003 — the projection.** `decodeEntry` reads only its members and
returns `#Entry`; the document is consumed and dropped inside the function. The
uid is stored once, `gvr` and `namespace` live in the scope, labels are interned
per scope. `managedFields` and `annotations` are never read — no allowlist, so a
`Secret`'s `data` never reaches the projection.

`json::Value::member` returns a `const &` (`value.hpp:49`), so the decoder takes
`const json::Value &` and copies only the small fields; no moving accessor is
needed, which removes a risk from the earlier draft.

**FR-004 — retention by admission.** A scope admits up to `maxEntries` and the
store up to `maxTotalEntries` across `maxScopes` scopes. Past a ceiling the
object is refused and counted, the scope is published `truncated`, and nothing
stored leaves — so `rowCount` never shrinks under the scrollbar and no pin is
needed. An entry leaves only on `DELETED` or the reconciliation sweep.

The budget proof is the two inequations in `#StoreBudget`; with the defaults it
resolves to 104 497 152 bytes inside the 104 857 600 declared.

**FR-005 — one parse per frame.** `takeFrames` becomes a cursor over the
accumulator reporting consumed bytes; the accumulator compacts once per chunk
instead of once per call (`watch_event.cpp:128`). `decodeWatchEvent` gains a `string_view` overload
returning the projection directly, so neither the `substr` nor the subtree copy
happens. Asserted with integer counters: parses equal frames and compactions stay
at most one per chunk. Allocations are **not** bounded here — `Value::parse` takes
193 per 4 431 byte frame, and no counter fixes that. Only a selective scan does,
and it is a separate feature.

**FR-007 — reconciliation.** A 410 marks the scope stale. The relist accumulates
a seen-set over the pages and reconciles at the end: absent uids removed, seen
ones inserted or updated. The model keeps identity, so selection and scroll
survive. `stale`, `truncated` and `refused` are scope state, carried by the
CUE `#ScopeState`; `#Scope` is the key triple and stays separate.

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
clock. FR-004 is asserted on the refusal path and FR-009 on the hop.

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
- **D-16 — versioned contract.** `#Entry` moves additively; the touch tick stays
  out so it is not frozen.
- **D-11 — additive surface.** `get` keeps `Object`; only the store holds
  projections.
- **P-03 — determinism.** The budget is counted, not measured by RSS.
- **D-02 — dogfood.** Every FR has a case in the suite the project runs.

## Companion Artifacts

- `contracts/` — none; the store is internal.
- `data-model.md` — not needed; `doc/arch/schemas/bound-the-event-memory.cue` is
  the model.
