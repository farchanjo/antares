---
revision: 2
last_reviewed: 2026-11-09
inheritance: strict
catalog: 2
amendments:
  - adr: "0001"
    date: "2026-11-09"
---
# antares Constitution

This document establishes the foundational principles and governance
model for the antares project. It guides every decision about
scope, contribution, and evolution of the system.

## Doctrines


## D-01 — Spec first
Severity: block
MANDATE: A feature is specified, planned and tasked before any production code is written, and the corpus is the source of truth over code and assumption. Run the lifecycle in order (specify → clarify → plan → tasks → implement) and keep the feature artifacts current.
Enforced by: llm-context.spec-first.missing, constitution.doctrines.missing, verify
## D-02 — Dogfooding
Severity: block
MANDATE: The project validates itself with its own commands and keeps the dogfood gate green; first rule that is right for consumers is right for the project. Run `speckit validate` and `speckit verify` before every commit; fix the artifact, never the rule.
Enforced by: arch_boundaries, validate, constitution.principle.duplicate
## D-03 — Hexagonal layering
Severity: block
MANDATE: Dependencies point one way: infrastructure to application to ports and domain, with the domain free of I/O. Tag the model element with its layer, break the cycle, and move the import to the allowed direction.
Enforced by: hexagonal.illegal-dependency, hexagonal.dependency-cycle, arch_boundaries, hexagonal.layers.untagged, layers.inward-import, layers.unknown-dir, layers.undeclared-import, layers.undeclared-dir, layers.cycle, layers.self-edge, layers.duplicate-edge, core.crate-denied, core.orm-import, core.crate-unadjudicated
## D-04 — Domain-driven design
Severity: block
MANDATE: Every model element declares its tactical role and every bounded-contact relationship declares one strategic kind. Declare the role, the aggregate root, the context pattern, and the typed context edge; fix membership and repository returns.
Enforced by: ddd-role.header.missing, ddd-role.role.unknown, ddd-tactical.missing-role, ddd-tactical.entity-identity, ddd-tactical.value-object-holds-identifiable, ddd-strategic.unknown-relationship, ddd-strategic.mutually-exclusive, ddd-strategic.mixed-relationship-sides, ddd-strategic.mixed-relationship-kinds, ddd-strategic.relationship-via-ref, ddd-strategic.relationship-deprecated, ddd-strategic.two-sources, context.pattern-missing, context.pattern-unknown, context.edge-untyped, context.property-orphan, context.on-layered-container, context.edge-reversed, context.kind-unknown, context.role-unknown, context.multiple-downstream-roles, context.roles-on-symmetric, context.conformist-in-customer-supplier, context.ohs-in-customer-supplier, context.acl-in-customer-supplier, aggregate.membership-missing, aggregate.root-unknown, aggregate.multiple-roots, aggregate.member-before-root, aggregate.cross-reference-object, repository.returns-member, repository.returns-tuple, doctrine.source-missing
## D-05 — Object Calisthenics
Severity: block
MANDATE: Entities stay small, names stay meaningful, nesting stays shallow, and primitives and collections are wrapped. Split the entity, rename the binding, extract the nested block, and wrap the bare primitive or collection.
Enforced by: calisthenics.small-entities, calisthenics.no-abbreviation, calisthenics-code.small-entities, calisthenics-code.no-abbreviation, calisthenics-code.nesting-depth, calisthenics-code.getter-setter, calisthenics.wrap-primitives, calisthenics.first-class-collection, calisthenics.one-definition-per-file, calisthenics-code.wrap-primitives, calisthenics-code.first-class-collection, calisthenics-code.one-type-per-file, calisthenics-code.no-else, branch.else, chain.one-dot, comment.implementation-in-interface, comment.repeats-code, condition.negative, interface.fat, literal.magic-number, name.hard-to-describe, name.vague, switch.type-code, type.boolean-flag, inheritance.depth, calisthenics-code.two-instance-variables
## D-06 — Fully qualified names
Severity: block
MANDATE: Every first-party production import is first full `crate::…` path; glob imports and `crate::domain::` are test-only conveniences. Replace `use x::*;` with the named items and `use crate::domain::y` with its `crate::…` path.
Enforced by: fqcn.glob-import, fqcn.relative-super
## D-07 — Enforced lints and exact pinning
Severity: block
MANDATE: Every detected ecosystem ships its lint configuration at the strongest level and pins its dependencies exactly. Commit the lint configuration and replace every range or floating version with an exact pin.
Enforced by: lint.rust.lints-missing, lint.js.eslint-missing, lint.ts.strict-missing, lint.java.config-missing, lint.go.golangci-missing, lint.c.clang-tidy-missing, lint.python.ruff-missing, lint.php.phpcs-missing, lint.react.plugin-missing, lint.html.htmlhint-missing, pin.cargo.lockfile-missing, pin.npm.save-exact-missing, pin.npm.range-version, pin.maven.dynamic-version, pin.gradle.dynamic-version, pin.python.range-version, pin.docker.unpinned-image, pin.ci.unpinned-action
## D-08 — Fail loud
Severity: block
MANDATE: Production code never panics on recoverable absence: `unwrap` and `expect` are denied, and an error is propagated or typed. Return first `Result`/`Option` up the stack, add first typed domain error, or handle the absence explicitly.
Enforced by: calisthenics-code.bare-unwrap, no-unwrap, clippy
## D-09 — Declared licence
Severity: block
MANDATE: A project declares its licence id and commits first matching `LICENSE` file; the two never drift. Run `speckit license set <id> --write-file` and keep the declared id equal to the committed text.
Enforced by: project-license.file.missing, project-license.fingerprint.mismatch
## D-10 — Guard-mediated writes
Severity: block
MANDATE: Every mutating command asks the guard before it writes, and the write lands inside the active scope. Call `GuardService::enforce_write` before the `FilesystemPort` write and surface the verdict as exit 4.
Enforced by: guard
## D-11 — Stable contracts
Severity: block
MANDATE: The published surface — exit codes, `--json` shapes, and declared artifact contracts — moves only additively. Add first field instead of renaming one, keep the exit table intact, and give every operation an id.
Enforced by: contract.info.missing, contract.operation.id-missing
## D-12 — Small changes
Severity: advisory
MANDATE: One concern per change: first commit, first branch and first review stay reviewable in one sitting. Split the work by concern, use the Angular commit header as the summary, and let the size advisories keep one unit from swallowing the change.
Enforced by: function.long, module.pass-through, module.special-general-mix, module.shallow
## D-13 — Offline by default
Severity: block
MANDATE: The core loop validates, phases and guards without the network; first network path is opt-in and degrades. Keep network access behind first port and first flag, and let first missing network fall back to the local behaviour; the core call table holds `std::net::` out of the core.
Enforced by: core.io-direct
## D-14 — Contract-first interface
Severity: block
MANDATE: A service declares its OpenAPI or AsyncAPI document before the implementation, and the document is the interface review. Author `doc/arch/contracts/<name>.yaml` with the info block and paths before writing handlers.
Enforced by: contract.info.missing, contract.openapi.version
## D-15 — Observable by default
Severity: block
MANDATE: A service ships an observability strategy and names its signals per feature, with bounded metric labels. Declare the strategy document, add the `## Observability` section to the feature spec, and bound the labels.
Enforced by: observability.strategy.missing, observability.strategy.section-missing, observability.cardinality.unbounded-label
## D-16 — Versioned and backward compatible
Severity: block
MANDATE: Every public contract declares its version and first breaking change is first new version, never an in-place edit. Bump the declared document version and keep the prior shape served until consumers migrate.
Enforced by: contract.openapi.version, contract.asyncapi.version
## D-17 — Reproducible dependency surface
Severity: advisory
MANDATE: A library publishes an exact, lockfile-anchored dependency set so first consumer resolves what its author tested. Commit the lockfile and pin direct dependencies to exact versions.
Enforced by: pin.cargo.lockfile-missing, pin.php.composer-lock-missing, pin.php.range-version, pin.npm.save-exact-missing, pin.maven.dynamic-version

## Principles

## P-01 — Clarity over cleverness
Severity: advisory
MANDATE: Every design choice must be explainable to a newcomer in a single paragraph.
Enforced by: config:guard, config:project, llm-context, verify
## P-02 — Constraints create focus
Severity: block
MANDATE: The project scope is bounded; features outside that scope belong in a separate initiative.
Enforced by: config:guard, config:project, llm-context, verify
## P-03 — Reproducibility by default
Severity: block
MANDATE: Any automated action must yield the same result when run twice on the same input.
Enforced by: verify

## P-04 — Single source root
Severity: block
MANDATE: Every production source file lives under `src/` at the project root, and `architecture.corePaths` names `src` so the layer and lint scanners read it. No source file sits at the root, under `doc/`, or in an ad-hoc directory; move the module under `src/` and register the path in configuration.
Enforced by: config:architecture, verify

## Governance

Changes to this constitution require a recorded Architecture Decision
(MADR) with status `accepted`, a non-empty `deciders` list, and an
`amendments` entry naming that ADR. Trivial corrections (typos,
formatting) may be committed directly. The MSRV pin and the frozen
`Cargo.toml [lints.*]` tables move only via accepted ADR.

## Maintenance

Last reviewed: 2026-11-09.
