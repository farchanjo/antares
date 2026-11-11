# Tasks: Bound The Event Memory

Each task lands with its conformance case. Memory is counted from an allocator,
never from RSS, so the case is deterministic.

## Task Breakdown

- [ ] T001 Commit the pod fixture `tests/k8s/fixtures/pod.json` (2 691 wire
      bytes, three labels, a 33-character name) and a worst-case object, then
      write the failing cases: 5 000 fixture pods allocate at most 2 560 000
      bytes through a counting allocator, and the worst-case entry at most
      1 024 bytes (FR-003).
- [ ] T002 Add `Entry`, `Identity`, `StatusSummary`, `Labels` and the ceilings to
      the domain, matching `bound-the-event-memory.cue` and
      `event-store-budget.cue` (FR-002).
- [ ] T003 Add `decodeEntry(const json::Value &)`: read only the members it
      needs, truncate labels at 16 entries or 256 bytes, refuse a name over 253
      bytes, and never read `managedFields` or `annotations` (FR-002, FR-003).
- [ ] T004 Replace `takeFrames` with a cursor over the accumulator that reports
      consumed bytes and compacts once per chunk; add `framesParsed` and
      `bufferCompactions` to `KubeStats` (FR-005).
- [ ] T005 Add the `string_view` overload of `decodeWatchEvent` that returns the
      projection, still mapping an ERROR frame to a typed failure; assert
      `framesParsed == 5 000` and `bufferCompactions <= chunks` (FR-005).
- [ ] T006 Change `ListPage` and `WatchEvent` to carry the projection; keep the
      port method signatures unchanged (FR-002).
- [ ] T007 Add `application::k8s::ObjectStore`: one scope per
      `(cluster, gvr, namespace)`, rows in arrival order plus a uid index,
      confined to one thread (FR-001, FR-009).
- [ ] T008 Make `upsert` replace in place and `erase` tombstone, compacting once
      per batch; add the case where two readers resolve the same uid and receive
      the same entry (FR-001, FR-006).
- [ ] T009 Emit one `ChangeSet` per batch and the case that replays it onto a row
      mirror and compares the rows (FR-008).
- [ ] T010 Add the per-scope and global admission ceilings with `truncated` and
      the refused count; the case fills a scope past its ceiling and asserts no
      stored entry leaves (FR-004).
- [ ] T011 Add epoch reconciliation over a paginated relist: the final page
      sweeps unmarked rows in one batch. Cases: the count after a 410 equals the
      relist, and a relist abandoned mid-way sweeps nothing (FR-007).
- [ ] T012 Route `ADDED`, `MODIFIED`, `DELETED` and `BOOKMARK` through the
      store; a `MODIFIED` keeps the count and emits one changed row (FR-006,
      FR-008).
- [ ] T013 Emit `store.entries` and `store.refused` with the two-value
      `scope_kind` label (D-15).

## Dependencies

T001 lands first and stays red until T010. T002 and T003 block the store. T004
and T005 do not depend on the store and can land first. T006 lands before T011
and T012, because the relist and the deltas reach the store as projections. T007,
T008 and T009 land in that order: the change set needs the row order, and removal
ranges need the per-batch compaction. T010 lands before T011, so a relist past
the ceiling is admitted by the same rule as a delta. Feature 004 does not block
this one; FR-009 keeps the store correct whether or not ADR-0020 is accepted.
