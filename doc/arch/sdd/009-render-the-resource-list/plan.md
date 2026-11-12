# Implementation Plan: Render The Resource List

## Overview

A `QAbstractListModel` that reads the store and holds nothing, and a `QListView`
configured so layout never blocks. The model is a lens; the store is the data.

## Technical Approach

**Layers.** `adapters/ui/` holds the model and the view. `adapters` may import
`application` and `ports` (layer table), so the model reads `ObjectStore&` and
the client. The domain never sees Qt.

**FR-001 — no row data.** `rowCount` returns the store's row count for the scope;
`data`/`multiData` resolve the row to a `const #Entry&`. A case compares the
model's `sizeof` plus its container against 4 KiB over five thousand rows.

**FR-002 — fetch.** `canFetchMore` returns true while the store holds a continue
token; `fetchMore` submits the next page with `limit` 500 and inserts the result
inside `beginInsertRows`/`endInsertRows`. The fetch is asynchronous: the page
completion posts the insert back to the GUI thread.

**FR-003 — roles.** `multiData` fills the `QModelRoleDataSpan` for name,
namespace, status and age in one call.

**FR-004 and FR-005 — the change set.** One function maps a `ChangeSet` to model
operations in the order Qt requires: removals descending, then insertions
ascending, then changes. A change whose mask carries the ordering key goes
through `beginMoveRows`, and the destination follows the documented rule: moving
down, `destinationChild` is the target plus one.

**FR-006 and FR-007 — view configuration.** `Batched`, `uniformItemSizes(true)`,
`Fixed` widths from a saved layout. A test asserts `uniformItemSizes` flips to
false when a row's height differs.

**FR-008 — threading.** Every mutation runs on the GUI thread. A `ChangeSet`
produced elsewhere crosses by `QMetaObject::invokeMethod` with
`Qt::QueuedConnection`; the model never reads the store off-thread.

### Test strategy

The model is testable without a window: an `ObjectStore` fixture, a recording
sink, and assertions on the operations a view would receive. The fetch case drives
`canFetchMore`/`fetchMore` directly.

**No `QApplication`, and no Qt in the suite.** `Qt6Core` on the CI host is built
against libstdc++ while this tree links libc++, so a `QString`-returning call is
an undefined symbol; a widget test would not link. The model therefore speaks a
plain `ModelSink`/`ModelWakeup` pair that mirrors `begin*`/`end*` and the queued
invocation, and `adapters/ui/qt_list_model.*` is the only translation unit that
includes Qt — behind `ANTARES_HAS_QT`, so a checkout without the toolkit still
builds and still runs every case.

### Risks

- `beginMoveRows` silently returns false on a bad destination; the case must
  assert the move happened, not that the call was made.
- A fetch failure leaves a hole; the model shows a retry row and must not retry
  in a loop.
- `multiData` has a default that falls back to `data`; overriding only `multiData`
  and forgetting `data` breaks delegates that call `data` directly.
- The store publishes *after* it mutates, so the model cannot bracket a batch
  itself. Every operation is stated with its indices instead of derived from
  `rowCount`, which is what makes an already-applied batch safe.
- A reorder is the one operation a `ChangeSet` cannot state. The model re-places
  the row in the store (`ObjectStore::moveToTail`) and then emits the move, so the
  view row index still equals the store row index.

## Constitution

- **P-12 as amended** — the view does not own the model, the model does not own
  the store; `QObject` children take parents, and a weak reference is `QPointer`.
- **D-03** — the model lives in `adapters`, not `ports`; Qt stays out of the
  inner layers.
- **D-05** — one class per file; the role mapping is a named function, not a
  magic index.
- **P-13** — the model and view are `QObject` children of the window, so the leak
  pass covers them.
- **D-15** — `pagesFetched` is a counter, not a label.

## Companion Artifacts

- `contracts/` — none; the model is internal.
- `data-model.md` — not needed; `render-the-resource-list.cue` names the roles.
