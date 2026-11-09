---
status: accepted
date: 2026-11-09
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0010 — Six log levels, mandatory and always declared

## Context and Problem Statement

A network engine fails in ways a debugger shows badly: a readiness filter fires
twice, a completion runs after teardown, a connection stalls between submit and
event. Without a decided level per event, the developer adds `printf` and removes
it later, and the release ships silent.

Levels only help when the vocabulary is fixed and every call site picks one on
purpose. Left to taste, `warn` becomes noise and `debug` becomes everything.

## Decision Drivers

- A stalled connection must be diagnosable from logs alone.
- The same source must serve development verbosity and release quiet.
- Six names cover the range; more invite debate, fewer conflate.
- Logging must not cost when disabled.

## Considered Options

- **Six fixed levels, mandatory category and level per call** — one vocabulary.
- **Free-form severity strings** — no filtering discipline.
- **`printf` plus a debug build** — no runtime control.

## Decision Outcome

Chosen option: **exactly six levels, declared per call site, filtered at
runtime.**

The levels are `trace`, `debug`, `info`, `warn`, `error` and `panic`, in that
order. `panic` logs and then aborts; it is for a state proven unreachable, never
for a recoverable absence (D-08).

Every module declares a category equal to its source directory, and every call
names a level. A call site without a level does not compile, because the level is
the first parameter of the macro family.

Selection is `ANTARES_LOG` (`<level>` or `<category>=<level>,…`), default
`info`. `trace` and `debug` compile out of `release` unless
`ANTARES_LOG_COMPILED_LEVEL` raises the floor. A disabled level costs one
predictable branch: `log::isEnabled(category, level)` guards hot loops.

Records are `timestamp level category message` plus `key=value` pairs. They go to
stderr through a sink port in `src/ports/log/`; the domain never includes the
sink implementation and never calls `fprintf`.

Secrets never reach a record: no cluster token, no certificate body, no
`kubeconfig` content. A handle or a length is logged in their place.

### Consequences

- Good: one vocabulary, filterable, same in every module.
- Good: a release can be re-run verbose without a rebuild.
- Good: the level is mandatory at the call site, so it is reviewed.
- Bad: a macro family to maintain.
- Bad: `panic` aborts, so a badly placed one turns a warning into a crash.
- Bad: categories must stay in step with directories.

### Confirmation

`src/ports/log/` declares the six levels and the sink port; a call site that names
no level fails to build, and `ANTARES_LOG=trace` reveals the engine's submit and
completion events.
