---
status: proposed
date: 2026-11-11
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0023 — Relax the API path name

## Context and Problem Statement

`ApiPath::named` validates the object name as a DNS-1123 subdomain
(`api_path.cpp:130-136`). The API server does not: for `metadata.name` it
requires a path segment — 1 to 253 bytes, no `/` and no `%`, and not `.` or
`..`. Uppercase, `_` and `:` are legal.

The app therefore refuses names clusters actually hold. `system:controller:persistent-volume-binder`
is the canonical one; a ConfigMap with uppercase is another.

Feature 005 stores those names (`#ObjectName`) and then cannot fetch them, so the
list shows a row the `get` refuses. The gap is ours, not the server's.

## Decision Drivers

- A row the app shows must be openable.
- The refusal must be the server's rule, not a stricter guess.
- Widening must not reopen path traversal.
- The namespace is different: the server does require DNS-1123 for it.

## Considered Options

- **Keep DNS-1123 for both.** No change, and a class of objects stays
  unreachable.
- **Percent-escape instead of validate.** The name comes from the server, so it
  is already a legal segment; escaping adds a second encoding the server does not
  expect for `%` or `/`.
- **Validate as a path segment.** Accepts what the server accepts and keeps the
  traversal property by refusing `/`, `%`, `.` and `..`.

## Decision Outcome

Chosen option: **validate as a path segment.**

`named` accepts 1 to 253 bytes with no `/` and no `%`, refusing `.`, `..` and the
empty string. `inNamespace` keeps the DNS-1123 label rule, because the server
enforces it there — the two validators diverge on purpose and the divergence is
recorded.

The refusal reports which rule broke instead of the single current message, and
the error stays an `ApiError` with the `rejected` reason.

### Consequences

- Good: the objects the cluster holds become reachable.
- Good: the store and the path stop disagreeing.
- Good: the traversal property is stated as a test instead of being a side effect
  of a stricter rule.
- Bad: two validators in one file, and a reader has to know which applies where.
- Bad: a name with `%` is still refused, so a resource whose name carries one
  stays unreachable — acceptable, because the server decodes `%` before routing.

## Pros and Cons of the Options

### Keep DNS-1123 for both

No change and no risk: a class of objects stays unreachable, and the store from
feature 005 keeps a row it cannot open.

### Percent-escape instead of validate

Removes the refusal entirely and adds an encoding. The server decodes `%` before
matching, so an escaped name that already contained `%` needs care; the
validation route avoids the question.

## More Information

Related: ADR-0017 (the API client, which owns the path build), ADR-0022 (the
store that accepts the wider name and depends on this).

The server rule is documented under "names" in the API conventions: a name is a
path segment, 253 bytes at most.
