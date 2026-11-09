---
status: accepted
date: 2026-11-09
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0002 — macOS first, C++ and Qt only, architecture model authoritative

## Context and Problem Statement

antares targets three desktop operating systems. Carrying all three from the
first commit splits attention across three packaging chains, three platform
APIs and three sets of window-manager quirks before one target works.

The stack is also open-ended unless pinned: C++ admits any toolkit, and Qt
admits QML, widgets, and third-party UI layers. Every admitted alternative is
a decision the corpus has to carry.

Finally, the architecture model under `doc/arch` only gates anything when the
project declares it as the authority and points the scanners at the same tree.

## Decision Drivers

- One target finished beats three targets half-done.
- macOS is the strictest platform: bundle, notarization, sandbox, menu bar.
  Shipping it first forces the hard problems early.
- One language and one toolkit keep review and tooling single.
- The layer table and the scanners need a declared root and a declared
  direction to have any teeth.

## Considered Options

- **macOS first, then Linux, then Windows** — ordered, reviewable, one
  packaging chain at a time.
- **All three from the start** — even coverage, three times the surface.
- **Linux first** — cheapest to iterate, defers the hard platform.
- **C++ with several toolkits** — flexible, no shared idioms.
- **C++ with Qt, QML admitted freely** — two UI paradigms to review.

## Decision Outcome

Chosen option: **macOS first, C++ with Qt only, architecture model
authoritative**.

**macOS first.** macOS is the delivery target until the first release ships.
Linux follows, Windows last. A feature is accepted on macOS before another
target is claimed; platform-specific code lands behind the platform layer, not
inline.

**C++ and Qt only.** The application is C++ compiled against Qt. No second
toolkit and no second language. Qt Widgets is the UI stack; QML enters only
through a later ADR. The domain layer stays free of Qt types, so the domain
compiles without the framework.

**The architecture model is authoritative.** Layers run inward as declared in
`architecture.layerOrder` (`domain`, `application`, `ports`, `adapters`), the
scanners read `src` from `architecture.corePaths`, and platform code lives in
`src/platform/<os>`. An import that breaks the direction moves, or the table
widens through an ADR.

### Consequences

- Good: one packaging chain until the first release.
- Good: the layer table, the guard scope and the scanners name one tree.
- Bad: Linux and Windows users wait; the corpus states the order so the wait is
  explicit.
- Bad: a domain type that needs Qt is a modeling problem, not an import.
- Bad: adding QML later costs an ADR and a review of the UI split.

### Confirmation

`speckit validate` reports a finding when source sits outside
`architecture.corePaths`, and the constitution carries these rules as `P-05`,
`P-06` and `P-07`.
