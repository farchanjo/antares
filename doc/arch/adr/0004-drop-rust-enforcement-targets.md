---
status: accepted
date: 2026-11-09
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0004 — Drop the remaining Rust enforcement targets

## Context and Problem Statement

ADR 0003 ported the mandates of D-06, D-07 and D-08 to C++, but left their
enforcement targets untouched for catalog parity. Those targets still name
Rust tooling: `lint.rust.lints-missing`, `pin.cargo.lockfile-missing`,
`no-unwrap` and `clippy`. The governance section names the MSRV pin and the
`Cargo.toml [lints.*]` tables in the same way.

The project holds no Rust. A target that only fires on a toolchain the project
does not install is dead weight in review, and it implies a build that does not
exist.

## Decision Drivers

- A rule cites what the project actually runs.
- Review reads the enforcement table as the checklist.
- Removing a dead target changes no behaviour: the validator still runs, the
  doctrine simply stops owning it.

## Considered Options

- **Drop the Rust targets** — table matches the toolchain.
- **Keep them for parity** — harmless, but misleading.
- **Drop every non-C++ target** — wider than asked; other ecosystems may
  return.

## Decision Outcome

Chosen option: **drop the Rust targets and reword the governance line**.

D-07 keeps `lint.c.clang-tidy-missing`, `pin.docker.unpinned-image` and
`pin.ci.unpinned-action`.

D-08 keeps `calisthenics-code.bare-unwrap`.

D-17 is reworded around CMake fetches pinned to an exact tag or commit hash,
with third-party code vendored under `src/adapters`, and keeps
`pin.ci.unpinned-action` and `pin.docker.unpinned-image`.

The governance section pins the C++ standard and the frozen `clang-tidy`
configuration instead of the MSRV and the Cargo lint tables.

### Consequences

- Good: the enforcement table names only tools the project installs.
- Good: no rule implies a Rust build.
- Bad: if Rust ever returns, the targets come back through a new ADR.
- Bad: `calisthenics-code.bare-unwrap` keeps the word `unwrap` in its id; the
  id is the catalog's, not the project's.

### Confirmation

`speckit validate` stays green and no `Enforced by` row names `cargo`,
`clippy`, `no-unwrap` or `pin.rust`.
