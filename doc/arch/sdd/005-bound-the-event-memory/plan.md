# Implementation Plan: Bound The Event Memory

## Overview

The decoded shape costs 7.51× the wire and no view reads most of it. The plan
replaces a retained document with a projection keyed by `uid`, in one store per
scope, and removes the two copies the watch path takes per frame.

The measured baseline in `spec.md` is the target: 19 528 bytes per object today,
under 512 after.

## Technical Approach

**Layers.** The projection and the store are `domain` values plus an
`application` service; nothing here opens a socket. The `ports::k8s` `Reader`
keeps its signature — `ListPage` and `WatchEvent` carry the projection instead
of the document, which is a change inside the port's types, not to its shape.

**FR-001 and FR-006 — identity.** `#ObjectKey` is
`(gvr, namespace, uid)`. `application::k8s::ObjectStore` owns
`std::unordered_map<#ObjectKey, #Entry>` per scope plus a monotonic tick so
eviction has an order. `upsert` replaces in place; `erase` removes; a bookmark is
a no-op on the map and updates only the scope's `resourceVersion`.

**FR-002 and FR-003 — the projection.** `decodeEntry(Value &&)` reads the
members it needs and returns `#Entry`. The store never sees the document; the
parse result is consumed and dropped inside the same function. Labels and
annotations are truncated at a fixed ceiling before they are stored.

**FR-004 — retention.** Per-scope `maxEntries` with least-recently-updated
eviction, driven by the tick. A relist builds a replacement map and swaps it, so
the count never sums.

**FR-005 — one parse per frame.** `decodeWatchEvent` gains an overload that takes
a `std::string_view` into the accumulator and returns the projection directly,
so neither the `substr` nor the subtree copy happens. `takeFrames` becomes a
cursor over the buffer that reports consumed bytes, and the accumulator compacts
once per chunk instead of once per frame.

**FR-007 — expiry.** A 410 marks the scope stale. The caller relists into a fresh
map and swaps.

### Alignment with ADR-0020

The parse moves to the main thread once the loop is hosted, so FR-005 is a
latency fix as much as a memory one: 11.9 µs per frame at 5 000 frames/s is 60 ms
per second of main-thread time.

### Test strategy

One conformance case per FR in `tests/k8s/`. The memory bound is asserted by
storing 5 000 synthetic pods and comparing `sizeof(#Entry)` plus the allocated
string bytes against 2.5 MB — no RSS, so the case is deterministic
(P-03) and portable. The parse count is asserted with a counter in the decoder,
not with a clock.

### Risks

- The projection drops a field a later view needs; the answer is an on-demand
  `get`, not a retained document, and that has to be written in the spec before
  the view exists.
- Truncating labels changes what a selector-shaped view can show; the ceiling
  must be well above what a navigation tree needs.
- `decodeEntry(Value &&)` moves out of the parsed tree, so any other reader of
  the same document must run before it.

## Constitution

- **D-05 — primitives are wrapped.** `#Text`, `#Name` and `#Flag` carry the
  scalars; `#Entry` stays under the field ceiling by naming an `#Identity`.
- **D-15 — bounded labels.** The store reports `entries` and `evictions` with a
  two-value `scope_kind`; per-object bytes are asserted by the test, never
  labelled.
- **D-16 — versioned contract.** `#Entry` moves additively: a new field is added,
  an existing one is never renamed in place.
- **P-03 — the same input gives the same result.** The memory bound is measured
  from the type and its allocations, not from RSS.
- **D-02 — the project validates itself.** Every FR gets a case in the same suite
  the project dogfoods.

## Companion Artifacts

- `contracts/` — none; the store is internal and the port keeps its shape.
- `data-model.md` — not needed; `doc/arch/schemas/bound-the-event-memory.cue`
  is the model.
