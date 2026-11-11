---
status: proposed
date: 2026-11-11
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0026 — Render the list from the store, not from a copy

## Context and Problem Statement

The store from ADR-0022 holds a projection per uid. Something has to render it.

The obvious route is a `QStandardItemModel`: one item per cell, `QVariant` inside.
For five thousand rows of four columns that is twenty thousand items on top of a
store that already holds every entry — a third copy of the same data, and the
largest one per row.

Qt also documents that a model is not thread-safe and that mutations belong on
the thread the model lives on, so the crossing has to be a value, not a call.

## Decision Drivers

- One copy: the store is the projection, the view is a lens.
- A list must fill without freezing on open.
- Selection and scroll must survive an update.
- The store already bounds count and string length; the view must not undo that.

## Considered Options

- **`QStandardItemModel`.** Fastest to write, and a third copy with a `QVariant`
  per cell.
- **A model over the store.** No row data in the model; `data()` reads the store.
- **A model over raw documents.** Keeps the store out of the render path and
  re-parses per access.

## Decision Outcome

Chosen option: **a model over the store, holding no row data.**

`rowCount` and `data` derive from the store. Pages arrive through
`canFetchMore`/`fetchMore` with `limit` 500, which is the pagination ADR-0022
already requires, driven by the view rather than by a timer. Roles are filled by
`multiData` in one call per row.

A `ChangeSet` maps one to one onto model operations: removed ranges to
`beginRemoveRows`, inserted to `beginInsertRows`, changed rows to `dataChanged`
with only the roles the mask names. A change to the ordering key is
`beginMoveRows`, so selection and scroll survive.

The view is configured for layout that does not block: `Batched` with the default
batch size, and `uniformItemSizes` true while every row keeps one height. Column
widths are `Fixed`; `ResizeToContents` is not used above a thousand rows.

### Consequences

- Good: the model's own footprint stays under 4 KiB for five thousand rows.
- Good: a list fills while it is already interactive.
- Good: an unrendered change costs no repaint.
- Bad: `data()` returns `QVariant` by value, so a `QString` over 22 bytes
  allocates per role even with `multiData`. Four allocations per painted row.
- Bad: `uniformItemSizes` is a promise; a row that varies makes the layout wrong
  until it is reset.
- Bad: the model must know the store's ordering, which couples the two.

## Pros and Cons of the Options

### `QStandardItemModel`

Nothing to write, and twenty thousand items with `QVariant` each. It also has to
be refilled on every relist.

### A model over raw documents

Saves the store but pays a parse per access; with the loop on the main thread a
scroll becomes thousands of parses. ADR-0022 already rejected it for that reason,
and the store exists.

## More Information

Related: ADR-0022 (the store and the projection), ADR-0020 (the hosted loop, which
puts the render on the main thread), ADR-0025 (the scanner that fills the store).

Qt documentation: `QAbstractItemModel` on incremental population and thread
safety, `QListView` on `layoutMode`, `batchSize` and `uniformItemSizes`.
