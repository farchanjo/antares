---
id: 01a1cbf4-a71d-7b90-b3e1-2c4f8a9d61e7
number: 006
slug: relax-api-path-name
status: analyzed
created_at: 2026-11-11T21:20:00.000000000Z
---
# Feature Specification: Relax The API Path Name

Feature: 006-relax-api-path-name
Created: 2026-11-11

## User Stories

- As the operator, I want to open a pod whose name carries a colon, so the
  objects the cluster actually has are reachable.
- As the operator, I want a name the server accepted to be fetched, so the app
  never shows a row it cannot open.
- As the developer, I want the path builder to reject exactly what the server
  rejects, so a refusal is real and not our guess.

## Problem

`ApiPath::named` validates the object name as a DNS-1123 subdomain
(`api_path.cpp:130-136` via `isDnsSubdomain`). The API server does not: for
`metadata.name` it requires a path segment — 1 to 253 bytes, no `/` and no `%`,
and not `.` or `..`.

The app therefore refuses names the cluster holds. Real examples: `kube-root-ca.crt`
is fine (dots pass today), but `system:controller:persistent-volume-binder` and
any RBAC-ish name with `:` are refused, and so are names with uppercase or `_`.

The store from feature 005 accepts those names (`#ObjectName`,
`bound-the-event-memory.cue`), so today the store can hold a row the `get` cannot
address. That is the gap this closes.

## Scope

The name validator behind `ApiPath::named` and `inNamespace`, and the error it
reports. The namespace stays a DNS-1123 label — the server does require that for
a namespace — so the two validators diverge on purpose.

Out of scope: the group and version validators, which the server does constrain
as DNS names, and the wire encoding of the path.

## Functional Requirements

### FR-001 The object name follows the server rule

`named` accepts any name of 1 to 253 bytes with no `/` and no `%`, and refuses
`.`, `..` and the empty string. Uppercase letters, `_`, `:` and `.` are accepted.

### FR-002 The namespace stays a DNS-1123 label

`inNamespace` keeps the current label rule (lower case alphanumeric and `-`,
1 to 63 bytes). A name that is legal for an object is still refused as a
namespace when it carries a colon or uppercase.

### FR-003 The refusal names the reason

A refused name reports which rule it broke — empty, too long, `.`, `..`, `/` or
`%` — instead of the current single "not a DNS-1123 subdomain" message. The error
stays an `ApiError` with the existing `rejected` reason.

### FR-004 The traversal property survives

`%` is refused because an encoded separator would reach the server unescaped, and
`.`/`..` because they resolve as path segments. A name built today cannot escape
its resource segment, and that is the property the DNS-1123 rule was standing in
for.

### FR-005 The store and the path agree

Every name `#ObjectName` accepts is accepted by `named` and the reverse, except
the server-side length the store does not re-check. A conformance case feeds one
name per class (upper, `_`, `:`, `.`, 253 bytes, 254 bytes, empty, `.`, `..`,
`a/b`, `a%b`) to both and asserts they agree.

## Security Requirements

- **Data sensitivity/classification.** The name is not sensitive; it appears in
  every path and log.
- **Authentication/authorization.** Not applicable — no new surface.
- **Input validation.** This *is* the input validation feature. FR-001 and FR-004
  state the accepted set and the reason for each refusal, so widening the rule
  does not widen traversal.
- **Cryptography in transit/at rest.** Not applicable.
- **Logging/audit.** A refusal is logged at `debug` with the reason class, never
  the whole name.
- **Error-handling information exposure.** The refusal names the rule broken and
  echoes the name, which the operator supplied.

## Acceptance Scenarios

Given the name `system:controller:persistent-volume-binder`
When  the path is built
Then  it is accepted.

Given the name `..`
When  the path is built
Then  it is refused as a parent segment.

Given the name `a/b`
When  the path is built
Then  it is refused as a separator.

The executable form lives in
`doc/arch/specs/features/relax-api-path-name.feature`.

## Observability

No new signal. The existing refusal path keeps its `debug` log; the reason class
becomes a closed set so the log line stays bounded.

## Clarifications

- **Why not escape instead of refuse?** The caller builds a path from a name the
  server gave back, so the name is already a legal segment. Escaping would add a
  second encoding the server does not expect for `%` and `/`.
- **Why keep the namespace strict?** The server does require DNS-1123 for a
  namespace, so relaxing it there would move the failure server-side.
