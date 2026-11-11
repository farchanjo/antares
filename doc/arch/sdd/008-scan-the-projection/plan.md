# Implementation Plan: Scan The Projection

## Overview

A scanner that reads the projection straight out of the bytes. It replaces the
`parse` plus projection pair on the watch and list paths; `Value::parse` stays for
everything else and as the fallback.

## Technical Approach

**Where it lives.** `src/domain/json/` gains `ScalarScan`, a cursor over a
`std::string_view` with `skipValue()` and the three decode helpers. `decodeEntry`
moves onto it. It allocates only for the fields the entry keeps.

**FR-002 — skip without allocating.** `skipValue` walks a value by shape and
returns; `managedFields`, `annotations`, `spec` and `Secret.data` never touch the
allocator. The case counts allocations through a counting allocator and fails
above the measured number.

**FR-003 — skipping validates.** The skip checks the delimiters it walks and
reports the byte offset of the first mismatch. Depth is bounded so a hostile
document cannot recurse without limit.

**FR-004 — escapes.** A kept string is unescaped into the entry: `\"`, `\\`,
`\/`, `\b`, `\f`, `\n`, `\r`, `\t` and `\uXXXX` with surrogate pairs. An escape
the projection keeps and the scanner mishandles is the likeliest defect, so the
fixtures carry one case per escape.

**FR-005 — key lookup.** A separate entry point walks `metadata` then `name` and
stops, so naming a resource costs a field and not a frame.

**FR-006 — the measurement.** The case runs both paths over the same fixture and
fails under 4×.

### Risks

- The scanner and the parser can drift; FR-001 compares them field by field over
  a fixture set, and it is the guard, not a formality.
- Deep nesting is a stack risk; depth is bounded and a case exceeds it.
- A `\uXXXX` surrogate pair split across a chunk boundary is the awkward case; it
  is decoded from the whole document, never per chunk.

## Constitution

- **D-08 — no panic.** Every failure is a `ParseError` with an offset; no abort.
- **D-05 — no magic numbers.** The depth bound and the escape table are named.
- **P-03 — determinism.** The comparison is field by field over fixed fixtures.
- **D-02 — dogfood.** The case runs in the same suite the project runs.

## Companion Artifacts

- `contracts/` — none; the scanner is internal to the domain.
- `data-model.md` — not needed; it produces `#Entry` from feature 005.
