---
revision: 10
last_reviewed: 2026-11-09
inheritance: strict
catalog: 2
amendments:
  - adr: "0001"
    date: "2026-11-09"
  - adr: "0002"
    date: "2026-11-09"
  - adr: "0003"
    date: "2026-11-09"
  - adr: "0004"
    date: "2026-11-09"
  - adr: "0005"
    date: "2026-11-09"
  - adr: "0006"
    date: "2026-11-09"
  - adr: "0010"
    date: "2026-11-09"
  - adr: "0011"
    date: "2026-11-09"
  - adr: "0012"
    date: "2026-11-09"
  - adr: "0014"
    date: "2026-11-09"
---
# antares Constitution

antares is a desktop Kubernetes manager in C++ and Qt. This document is the
rule root: it binds scope, contribution and evolution, and every other artifact
answers to it. A doctrine is how the project works; a principle is what it
refuses to trade away.

## Doctrines

## D-01 — Spec first
Severity: block
MANDATE: No production line lands before the feature is specified, planned and tasked; `doc/arch` outranks code and assumption, and the lifecycle runs specify → clarify → plan → tasks → implement.
Enforced by: llm-context.spec-first.missing, constitution.doctrines.missing, verify
## D-02 — Dogfooding
Severity: block
MANDATE: antares is checked by antares: the same gates the corpus demands of a consumer are run here, and the dogfood target stays green.
Enforced by: arch_boundaries, validate, constitution.principle.duplicate
## D-03 — Hexagonal layering
Severity: block
MANDATE: Infrastructure depends on application, application on ports and domain; the domain touches no I/O, and a model element carries its layer tag so the boundary scanner can see it.
Enforced by: hexagonal.illegal-dependency, hexagonal.dependency-cycle, arch_boundaries, hexagonal.layers.untagged, layers.inward-import, layers.unknown-dir, layers.undeclared-import, layers.undeclared-dir, layers.cycle, layers.self-edge, layers.duplicate-edge, core.crate-denied, core.orm-import, core.crate-unadjudicated
## D-04 — Domain-driven design
Severity: block
MANDATE: A model element names its tactical role, and every contact between bounded contexts names one strategic kind — role, aggregate root and relationship are declared, not implied.
Enforced by: ddd-role.header.missing, ddd-role.role.unknown, ddd-tactical.missing-role, ddd-tactical.entity-identity, ddd-tactical.value-object-holds-identifiable, ddd-strategic.unknown-relationship, ddd-strategic.mutually-exclusive, ddd-strategic.mixed-relationship-sides, ddd-strategic.mixed-relationship-kinds, ddd-strategic.relationship-via-ref, ddd-strategic.relationship-deprecated, ddd-strategic.two-sources, context.pattern-missing, context.pattern-unknown, context.edge-untyped, context.property-orphan, context.on-layered-container, context.edge-reversed, context.kind-unknown, context.role-unknown, context.multiple-downstream-roles, context.roles-on-symmetric, context.conformist-in-customer-supplier, context.ohs-in-customer-supplier, context.acl-in-customer-supplier, aggregate.membership-missing, aggregate.root-unknown, aggregate.multiple-roots, aggregate.member-before-root, aggregate.cross-reference-object, repository.returns-member, repository.returns-tuple, doctrine.source-missing
## D-05 — Object Calisthenics
Severity: block
MANDATE: Keep entities small, names pronounceable, nesting shallow; primitives and collections get a type, and a nested block becomes its own function.
Enforced by: calisthenics.small-entities, calisthenics.no-abbreviation, calisthenics-code.small-entities, calisthenics-code.no-abbreviation, calisthenics-code.nesting-depth, calisthenics-code.getter-setter, calisthenics.wrap-primitives, calisthenics.first-class-collection, calisthenics.one-definition-per-file, calisthenics-code.wrap-primitives, calisthenics-code.first-class-collection, calisthenics-code.one-type-per-file, calisthenics-code.no-else, branch.else, chain.one-dot, comment.implementation-in-interface, comment.repeats-code, condition.negative, interface.fat, literal.magic-number, name.hard-to-describe, name.vague, switch.type-code, type.boolean-flag, inheritance.depth, calisthenics-code.two-instance-variables
## D-06 — Fully qualified names
Severity: block
MANDATE: Every production include spells the path from `src/`; `domain/account.hpp` beats `../domain/account.hpp`, and a test may still climb.
Enforced by: fqcn.glob-import, fqcn.relative-super
## D-07 — Enforced lints and exact pinning
Severity: block
MANDATE: `clang-tidy` and `clang-format` are committed at their strongest setting, and a dependency is pinned to a tag or commit — never a branch, a range or a floating `latest`.
Enforced by: lint.c.clang-tidy-missing, pin.docker.unpinned-image, pin.ci.unpinned-action
## D-08 — Fail loud
Severity: block
MANDATE: Recoverable absence never aborts: an unchecked `value()`, an unproven `.at()`, a raw `new`/`delete` are denied, and the failure comes back as `std::expected` carrying a typed domain error.
Enforced by: calisthenics-code.bare-unwrap, no-unwrap, clippy
## D-09 — Declared licence
Severity: block
MANDATE: The licence id is declared and the matching `LICENSE` file committed; run `speckit license set <id> --write-file` and keep the two in step.
Enforced by: project-license.file.missing, project-license.fingerprint.mismatch
## D-10 — Guard-mediated writes
Severity: block
MANDATE: A mutating command asks the guard first, and the write lands inside the active scope: `GuardService::enforce_write` precedes the write, never follows it.
Enforced by: guard
## D-11 — Stable contracts
Severity: block
MANDATE: Exit codes, `--json` shapes and declared contracts only ever gain: a field is added before one is renamed, and a removal waits for the next version.
Enforced by: contract.info.missing, contract.operation.id-missing
## D-12 — Small changes
Severity: advisory
MANDATE: One concern per change. The first commit, first branch and first review fit in one sitting, and the Angular header summarises the concern.
Enforced by: function.long, module.pass-through, module.special-general-mix, module.shallow
## D-13 — Offline by default
Severity: advisory
MANDATE: The core loop validates, phases and guards with the network unplugged; the first network path is opt-in and degrades when it is absent. ADR 0014 supersedes D-13's earlier `block` severity, because no automated target checked it.
Enforced by: (manual)
## D-14 — Contract-first interface
Severity: block
MANDATE: A service publishes its OpenAPI or AsyncAPI document before the handler exists — `doc/arch/contracts/<name>.yaml` with the info block and paths is the interface review.
Enforced by: contract.info.missing, contract.openapi.version
## D-15 — Observable by default
Severity: block
MANDATE: A service carries an observability strategy and names its per-feature signals; a metric label has a bounded value set, and the strategy document is declared.
Enforced by: observability.strategy.missing, observability.strategy.section-missing, observability.cardinality.unbounded-label
## D-16 — Versioned and backward compatible
Severity: block
MANDATE: A public contract declares its version, and the first breaking change opens a new version instead of editing the old one in place.
Enforced by: contract.openapi.version, contract.asyncapi.version
## D-17 — Reproducible dependency surface
Severity: advisory
MANDATE: A library ships an exact, lockfile-anchored dependency set, so a consumer resolves what the author tested; direct dependencies are pinned.
Enforced by: (manual)

## Principles

## P-01 — Clarity over cleverness
Severity: advisory
MANDATE: A design choice survives being explained to a newcomer in one paragraph; if the paragraph needs a second, the choice is not ready.
Enforced by: config:guard, config:project, llm-context, verify
## P-02 — Constraints create focus
Severity: block
MANDATE: Scope is bounded. A feature outside it starts a separate initiative rather than widening this one.
Enforced by: config:guard, config:project, llm-context, verify
## P-03 — Reproducibility by default
Severity: block
MANDATE: The same input produces the same result on a second run; an action that cannot repeat is a defect.
Enforced by: verify
## P-04 — Single source root
Severity: block
MANDATE: Production sources live under `src/` and `architecture.corePaths` names it, so the layer and lint scanners actually read the tree.
Enforced by: config:architecture, verify
## P-05 — macOS first
Severity: block
MANDATE: macOS ships first until the first release; Linux follows, Windows last, and a feature is accepted on macOS before any other target.
Enforced by: config:project, verify
## P-06 — C++ and Qt only
Severity: block
MANDATE: The application is C++ against Qt Widgets with no second toolkit and no second language; QML enters only through an ADR.
Enforced by: config:project, verify
## P-07 — Architecture model is authoritative
Severity: block
MANDATE: Layers run inward in `architecture.layerOrder` — `domain`, `application`, `ports`, `adapters` — and platform code sits in `src/platform`.
Enforced by: config:architecture, verify
## P-08 — Fast dev loop, release on authorization
Severity: block
MANDATE: Development builds are the fast ones: Ninja, `sccache`, `lld`, `-O0 -g1`, precompiled headers, per-target builds. The optimized build is the gated one.
Enforced by: config:project, verify
## P-09 — Kernel primitive behind a port
Severity: block
MANDATE: Transport reaches the kernel only through `IoEngine` in `src/ports/io/`, never `QNetworkAccessManager`; the backend is chosen once at startup.
Enforced by: config:architecture, verify
## P-10 — Six log levels, always declared
Severity: block
MANDATE: A log call names one of `trace`, `debug`, `info`, `warn`, `error`, `panic` and the category of its source directory; `panic` logs and aborts.
Enforced by: config:project, verify
## P-11 — Newest C++ in full, no C escape hatch
Severity: block
MANDATE: The project compiles as C++26 and prefers the language to C: `std::expected`, `std::span`, `std::string_view`, `std::byte`, concepts, `constexpr`. A `void*`, a pointer-plus-count, a C array or a `#define` constant carries a reason.
Enforced by: config:project, verify
## P-12 — Ownership over pointers
Severity: block
MANDATE: A raw pointer is non-owning or `nullptr`; ownership is `unique_ptr`, and `shared_ptr` only where a lifetime is truly shared. A non-owning reference outlives its use or is not taken.
Enforced by: config:architecture, verify
## P-13 — Leak gate always on
Severity: block
MANDATE: Every run is leak-checked — `asan` for AddressSanitizer, `leak` for the leak pass — and the binary is symbolized.
Enforced by: config:project, verify
## P-14 — mimalloc, pinned, and never beside a sanitizer
Severity: block
MANDATE: Allocation goes through mimalloc, pinned by URL and SHA-256 at the version the ADR names, with `ANTARES_ALLOCATOR=system` as the escape hatch.
Enforced by: config:project, verify

## Governance

Amending this document takes a recorded MADR with status `accepted`, a
non-empty `deciders` list and an `amendments` entry naming that ADR. Trivial corrections (typos,
formatting) may be committed directly. The C++ standard pin and the frozen
`clang-tidy` configuration move only via accepted ADR.

## Maintenance

Last reviewed: 2026-11-09.
