# Implementation Plan: Relax The API Path Name

## Overview

One validator narrows to the server's rule for a name and stays strict for a
namespace. The change is inside `src/domain/k8s/api_path.cpp` and its header; no
caller changes.

## Technical Approach

**FR-001 and FR-004 — the name rule.** `isDnsSubdomain` stops guarding
`metadata.name`. A new `isPathSegment` accepts 1 to 253 bytes with no `/` and no
`%`, and refuses `.` and `..`. `named` calls it.

**FR-002 — the namespace.** `inNamespace` keeps `isDnsLabel`. The two functions
diverge on purpose and the header comment says which is which.

**FR-003 — the reason.** The single `rejected("name is not a DNS-1123 subdomain")`
becomes a closed reason class — `empty`, `tooLong`, `dot`, `dotdot`, `separator`,
`escape` — mapped by one function. The `ApiError` reason stays `rejected`.

### Test strategy

A table case over the eleven names in FR-005, plus the traversal cases. The
table is the contract: it feeds the same names to the store's `#ObjectName` rule
and asserts agreement, so the two cannot drift.

### Risks

- A name containing `%` stays refused, by design; a resource that carries one is
  unreachable and the ADR records it.
- `isDnsSubdomain` is still used for the group, so it does not disappear and a
  future reader may reach for it by mistake; the header names its remaining use.

## Constitution

- **D-08 — no panic on recoverable absence.** Every refusal is an `ApiError`;
  the new validator returns `bool` and the caller builds the error.
- **D-05 — no magic numbers.** `SEGMENT_CEILING` names the 253.
- **P-03 — determinism.** The table is fixed; no random name generation.
- **D-02 — dogfood.** The agreement case runs in the same suite the project runs.

## Companion Artifacts

- `contracts/` — none; the path is internal.
- `data-model.md` — not needed; `relax-api-path-name.cue` states the two rules.
