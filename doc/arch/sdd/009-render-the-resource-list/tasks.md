# Tasks: Render The Resource List

## Task Breakdown

- [x] T001 Add the model skeleton over `ObjectStore` with `rowCount` and `data`
      and the case that its footprint stays under 4 KiB for 5 000 rows (FR-001).
- [x] T002 Add `multiData` over the four roles, keeping `data` working for
      delegates that call it directly (FR-003).
- [x] T003 Add `canFetchMore`/`fetchMore` with `limit` 500 and the insert inside
      one `beginInsertRows`/`endInsertRows` (FR-002).
- [x] T004 Map a `ChangeSet` to removals descending, insertions ascending and
      changes (FR-004).
- [x] T005 Route an ordering-key change through `beginMoveRows` with the
      destination-plus-one rule; assert the move happened (FR-005).
- [x] T006 Configure the view: `Batched`, `uniformItemSizes(true)`, `Fixed`
      widths from a saved layout (FR-006, FR-007).
- [x] T007 Flip `uniformItemSizes` to false when a row height varies, with a
      case (FR-006).
- [x] T008 Deliver a `ChangeSet` from a non-GUI thread by queued invocation
      (FR-008).
- [x] T009 Add the retry row for a failed fetch and the case that it does not
      retry in a loop.
- [x] T010 Add `pagesFetched` to `KubeStats` (observability).

## Dependencies

T001 blocks everything. T002 before T006, which renders the roles. T003 needs the
store's continue token from feature 005. T004 and T005 need the `ChangeSet` from
feature 005. T008 needs the threading decision of feature 007.
