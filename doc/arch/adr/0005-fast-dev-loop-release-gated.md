---
status: accepted
date: 2026-11-09
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0005 — Fast development loop, release only on authorization

## Context and Problem Statement

C++ with Qt produces slow builds by default: deep template instantiation, large
translation units, and on macOS a linker that dominates wall time. A release
configuration adds LTO, full debug info, two architectures and notarization on
top of that.

If the inner loop runs the release configuration, every edit costs minutes and
the project stalls. If the loop runs nothing but unoptimized code, an artifact
can reach a user without optimization, without symbols, and without the checks
that only a release build performs.

## Decision Drivers

- Edit-compile-test cycles dominate the calendar.
- Release flags are the most expensive and the least frequently needed.
- A release artifact is a promise; a dev artifact is a draft.
- The fast path must be reproducible, not a local habit.

## Considered Options

- **Dev loop fast, release on authorization** — two presets, one gate.
- **One configuration for everything** — simple, slow.
- **Dev loop fast, release whenever convenient** — drift, no gate.

## Decision Outcome

Chosen option: **fast development configuration, release only on explicit
operator authorization**.

Development builds use: Ninja, `sccache` with `SCCACHE_BASEDIR` and
`-ffile-prefix-map` so the cache survives a path change, `lld` as linker,
`-O0 -g1`, precompiled headers on the stable set, per-target builds, and
`clang-tidy` restricted to the changed file in its own target.

Release builds add, and only here: `-O3`, thin LTO, full `-g` with dSYM,
universal `x86_64;arm64`, strip and notarization.

`CMakePresets.json` carries both as `dev` and `release`. No release artifact
ships from a `dev` build.

### Consequences

- Good: the inner loop stays in seconds.
- Good: release flags become a deliberate, reviewable event.
- Bad: a dev build is slower at runtime; performance work needs the release
  preset on purpose.
- Bad: two presets to keep in sync.
- Bad: forgetting the gate ships an unoptimized artifact to a user.

### Confirmation

The constitution carries the rule as `P-08`, and the build presets exist under
`CMakePresets.json` before the first source file lands.
