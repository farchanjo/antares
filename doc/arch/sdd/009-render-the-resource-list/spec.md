---
id: 01a1cbf4-d04a-7fc3-b6e5-8f7c2d4a0e62
number: 009
slug: render-the-resource-list
status: analyzed
created_at: 2026-11-11T23:05:00.000000000Z
---
# Feature Specification: Render The Resource List

Feature: 009-render-the-resource-list
Created: 2026-11-11

## User Stories

- As the operator, I want to scroll a large list while it fills, so the window
  never freezes on open.
- As the operator, I want the next page when I reach the end, so a list is not
  capped at whatever arrived first.
- As the reviewer, I want the view to hold no data of its own, so there is one
  copy and not three.

## Scope

The Qt model and the list view over the store from feature 005: how rows are
counted, fetched, rendered and updated. It consumes the store; it does not
change it.

Out of scope: the store, the projection, the transport, and every widget other
than the list.

## The three copies this avoids

A naive list holds the document, the store entry **and** a `QStandardItem` per
cell. Measured shape of the problem: five thousand rows of four columns is
twenty thousand items, each with a `QVariant`, on top of a store that already
holds the entry.

The model here holds **no row data**. `data()` reads the store by index; the
store is the only copy.

## Functional Requirements

### FR-001 The model holds no data

The model derives `rowCount()` and `data()` from the store. A `QStandardItemModel`
is not used. A case asserts the model's own footprint stays under 4 KiB for a
five thousand row scope, against the store's budget.

### FR-002 Pages arrive through `fetchMore`

`canFetchMore` is true while the store reports a `continue` token for the scope,
and `fetchMore` asks the client for the next page with `limit` 500. The added
rows are wrapped in `beginInsertRows`/`endInsertRows`. No timer drives the fetch;
`QAbstractItemView` calls it.

### FR-003 Roles are filled in one call

`multiData` fills the span for a row in one call instead of one `data()` per
role. The four roles are name, namespace, status and age.

### FR-004 A `ChangeSet` becomes row operations

The model maps a `ChangeSet` one to one: removed ranges through
`beginRemoveRows`, inserted through `beginInsertRows`, changed rows through
`dataChanged` carrying only the roles the field mask names. Several events for
one uid already arrive as one change (FR-008 of 005).

### FR-005 A reorder is a move, not a rewrite

When the change touches the ordering key, the model emits `beginMoveRows` rather
than a remove plus an insert, so selection and scroll survive. The
`destinationChild` rule is honoured: moving down means destination plus one.

### FR-006 Layout is batched and uniform

The view sets `layoutMode` to `Batched` with the default batch size, and
`uniformItemSizes` to true. `uniformItemSizes` may only stay true while every
row has the same height; a row that varies resets it to false.

### FR-007 The header never measures contents

Column widths are `Fixed` with saved widths. `ResizeToContents` is not used on a
scope above a thousand rows.

### FR-008 The model lives on the GUI thread

Every mutation happens on the thread the model lives on, which is the GUI thread.
A `ChangeSet` produced elsewhere crosses as a value through a queued connection
(D-05, P-12).

## Security Requirements

- **Data sensitivity/classification.** The view renders metadata already in the
  store: names, namespaces, status. It adds no field and therefore no exposure.
- **Authentication/authorization.** Not applicable — the view reads what the
  store already holds.
- **Input validation.** The untrusted input is the row count and the string
  lengths; both are bounded by the store (FR-003 of 005). The view truncates
  display text with elision, never by cutting the stored string.
- **Cryptography in transit/at rest.** Not applicable — no transport here.
- **Logging/audit.** A failed `fetchMore` logs at `warn` with the scope and the
  error family. No row content.
- **Error-handling information exposure.** A fetch failure shows a retry row; the
  message is the error family, not the payload.

## Acceptance Scenarios

Given a scope with five thousand stored objects
When  the model is built
Then  its own footprint stays under 4 KiB and no row data is copied.

Given a scope with a continue token
When  the view reaches the last row
Then  `fetchMore` requests the next page and the rows are inserted as one range.

Given a `ChangeSet` that changes an unrendered field
When  the model applies it
Then  no row is repainted.

The executable form lives in
`doc/arch/specs/features/render-the-resource-list.feature`.

## Observability

No new metric. The store already reports `entries` and `refused`; the view adds
`pagesFetched` to `KubeStats` so a fetch loop is visible.

## Clarifications

- **Why not `QStandardItemModel`?** One item per cell, each with a `QVariant`,
  is a third copy of data the store already holds.
- **Why `multiData`?** It is one call per row instead of four. It does **not**
  remove the `QVariant` copy — `data()` returns by value and a `QString` over 22
  bytes allocates. That cost is stated, not solved.
- **Why is `uniformItemSizes` conditional?** The documentation requires every
  item to be the same size; a varying row makes the hint a lie and the layout
  wrong.
