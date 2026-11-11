---
id: 01a1cbf4-c93f-7eb2-a5d4-7e6b1c3f9d51
number: 008
slug: scan-the-projection
status: analyzed
created_at: 2026-11-11T21:55:00.000000000Z
---
# Feature Specification: Scan The Projection

Feature: 008-scan-the-projection
Created: 2026-11-11

## User Stories

- As the operator, I want a burst of events to cost a fraction of what it costs
  today, so a busy cluster does not slow the window.
- As the developer, I want the projection read straight out of the bytes, so the
  document is never built for fields nobody renders.
- As the reviewer, I want the saving measured against the parser it replaces, so
  the claim is a number and not a hope.

## Problem

Feature 005 stores a projection but still parses the whole document first.
`decodeEntry` runs after `Value::parse`, which builds every member —
`managedFields` and `annotations` included — and then drops them.

Measured on a 4 431 byte pod frame, 39% of it `managedFields`:

| Path | Time | Allocations |
|---|---|---|
| `Value::parse` then project | 15.8 µs | 193 |
| Selective scan | **1.97 µs** | — |

The projection already knows which fields it reads. Reading them from the bytes
and skipping the rest without allocating is 8×, against 1.7× for `as=Table` — and
it needs no shape negotiated per kind, so a CRD benefits as much as a pod.

## Scope

A scanner in the domain that reads a document and produces a `#Entry`, plus the
key lookup it needs. The store, the framing and the transport are untouched.

Out of scope: the wire format, `as=Table` (a separate question), and any field
the projection does not carry.

## Functional Requirements

### FR-001 The scanner produces the same entry as the parser

For any document the current path accepts, the scanner produces an entry equal to
the one `parse` plus projection produces. A case runs both over a fixture set and
compares field by field.

### FR-002 Fields outside the projection are skipped without allocating

`managedFields`, `annotations`, `spec` and a `Secret`'s `data` are walked and
skipped. The allocation count per frame is a measured number and the case fails
above it, so a regression is caught.

### FR-003 A skipped value is still validated

Skipping is not trusting: braces, brackets, quotes and escapes are checked while
skipping, and a malformed document fails at the right byte offset. A case feeds a
truncated and a mis-nested document and asserts the offset.

### FR-004 Escapes are resolved for the fields that are kept

A kept string is unescaped — `\"`, `\\`, `\uXXXX`, the control escapes — so the
stored value is the decoded one. A name with an escaped character round-trips.

### FR-005 A key is found without building the document

The lookup that names a resource walks the path `metadata.name` and stops, so the
common case costs one field, not a frame.

### FR-006 The saving is measured, not assumed

The case reports both paths over the same fixture and fails if the scanner is
less than 4× faster. The floor is the conservative half of the 8× measured, so a
slow machine does not fail the build.

## Security Requirements

- **Data sensitivity/classification.** The scanner reads the same bytes the
  parser does and keeps less. `Secret.data` is skipped, so a secret never reaches
  an entry — this is now a property of the code path, not only of the projection.
- **Authentication/authorization.** Not applicable — no new surface.
- **Input validation.** This is the third parser in the tree and the one most
  exposed: FR-003 and FR-004 are the whole contract. Depth is bounded, and a
  document that ends inside a skip fails with its offset.
- **Cryptography in transit/at rest.** Not applicable — memory only.
- **Logging/audit.** A scan failure logs the offset and the rule, never the
  payload.
- **Error-handling information exposure.** The error carries an offset and a
  reason class, not the bytes.

## Acceptance Scenarios

Given the fixture pod
When  both the parser and the scanner read it
Then  the entries are equal field by field.

Given a document truncated inside a skipped field
When  the scanner reads it
Then  it fails with the offset of the truncation.

Given the fixture pod
When  both paths run over it 5 000 times
Then  the scanner is at least 4× faster.

The executable form lives in
`doc/arch/specs/features/scan-the-projection.feature`.

## Observability

No new signal. `KubeStats` gains `scanFailures`, so a document the parser accepts
and the scanner refuses is visible instead of silent.

## Clarifications

- **Why not `as=Table`?** It is 1.7× measured, keeps `managedFields` in the row,
  and needs a shape negotiated per kind. The scanner is local, works for a CRD,
  and is 8×.
- **Why not fold this into 005?** 005 is already specified, reviewed twice and
  carries a store; a third parser is its own risk (P-02).
- **What is the fallback?** If the scanner cannot match the parser on a document
  shape, that shape goes through `Value::parse` and the scanner is tried again
  after a fix. The entry type is shared, so a mixed path is legal.
