# Tasks: Bound The Event Memory

Each task lands with its conformance case. The memory bound is asserted from the
type and its allocations, never from RSS, so the case is deterministic.

## Task Breakdown

- [ ] T001 Write the failing measurement case: store 5 000 synthetic pods and
      assert the total stays under 2.5 MB (FR-003).
- [ ] T002 Add `#Entry`, `#Identity`, `#StatusSummary` and the truncation
      ceilings to the domain, matching `bound-the-event-memory.cue` (FR-002).
- [ ] T003 Add `decodeEntry` over a parsed document, moving rather than copying,
      and never reading `managedFields` (FR-002).
- [ ] T004 Add `application::k8s::ObjectStore` keyed by `(gvr, ns, uid)` with a
      monotonic tick (FR-001, FR-006).
- [ ] T005 Make `upsert` replace in place and `erase` remove; add the case where
      two readers resolve the same uid and receive the same entry (FR-001).
- [ ] T006 Add the relist-as-swap path so a 410 replaces the scope instead of
      merging it, with a case asserting the count equals the relist (FR-007).
- [ ] T007 Add per-scope `maxEntries` with least-recently-updated eviction and
      the case that exceeds the ceiling (FR-004).
- [ ] T008 Replace `takeFrames` with a cursor that reports consumed bytes, and
      compact the accumulator once per chunk (FR-005).
- [ ] T009 Add the `string_view` overload of `decodeWatchEvent` that returns the
      projection directly, with no `substr` and no subtree copy (FR-005).
- [ ] T010 Add a parse counter in the decoder and assert under 1.5 parses per
      frame over 5 000 frames (FR-005).
- [ ] T011 Route a `MODIFIED` delta through `upsert` and assert the entry count
      is unchanged (FR-006).
- [ ] T012 Change `ListPage` and `WatchEvent` to carry the projection; keep the
      port method signatures unchanged (FR-002).
- [ ] T013 Emit `store.entries` and `store.evictions` with the two-value
      `scope_kind` label (D-15).

## Dependencies

T002 and T003 block everything in the store; T008 and T009 are independent and
can land first. T012 changes the port's carried types, so it lands after T004 so
the store exists to receive them. Feature 004 does not block this one, but the
hosted loop (ADR-0020) depends on both.
