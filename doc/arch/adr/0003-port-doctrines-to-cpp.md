---
status: accepted
date: 2026-11-09
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0003 — Port D-06, D-07 and D-08 to C++ and Qt

## Context and Problem Statement

The mandatory doctrine catalog ships three rules written for a Rust tree:

- D-06 speaks of `crate::…` paths and glob imports.
- D-07 speaks of the detected ecosystem, with `cargo` and `clippy` implied.
- D-08 speaks of `unwrap` and `expect`.

antares is C++ with Qt. Read literally, the three rules name constructs the
project never writes, so the review has no text to check and the gates fire on
nothing.

## Decision Drivers

- A rule that names a foreign construct is not a rule, it is a reminder.
- The enforcement targets are fixed by the catalog; the mandate text is the
  part the project owns.
- The intent — explicit paths, strongest lints, exact pins, no blind abort —
  survives translation.

## Considered Options

- **Rewrite the three mandates for C++** — keeps the intent, matches the tree.
- **Leave them Rust-flavored** — no edit, permanent noise in review.
- **Drop them** — loses pinned dependencies and explicit includes.

## Decision Outcome

Chosen option: **rewrite the three mandates in C++ terms, keeping their
enforcement targets**.

D-06 keeps `fqcn.glob-import` and `fqcn.relative-super` and now requires the
full path from `src/` in every first-party include; a `../` climb and a bare
file name are test-only.

D-07 keeps the lint and pin targets and now names `clang-tidy` and
`clang-format` at the strongest level, with every dependency pinned to an exact
tag or commit hash.

D-08 keeps `calisthenics-code.bare-unwrap`, `no-unwrap` and `clippy`, and now
denies the unchecked `std::optional::value()`, the unproven `.at()`, raw `new`
and raw `delete`, requiring `std::expected` and a typed domain error.

### Consequences

- Good: the review reads the rule in the language the project writes.
- Good: the catalog targets stay untouched, so the gates keep their ids.
- Bad: a future non-C++ module translates again.
- Bad: `clippy` and `no-unwrap` remain in D-08 although they name no C++
  construct; they stay for catalog parity.

### Confirmation

`speckit validate` stays green, and `speckit constitution explain D-06`,
`D-07` and `D-08` render the ported mandates.
