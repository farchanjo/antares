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
MANDATE: Every first-party production include names the full path from `src/`; a `../` climb and a bare file name are test-only conveniences. `#include "domain/account.hpp"` wins over `#include "../domain/account.hpp"`, and an umbrella header never hides the real dependency.
Enforced by: fqcn.glob-import, fqcn.relative-super
## D-07 — Enforced lints and exact pinning
Severity: block
MANDATE: The project commits `clang-tidy` and `clang-format` at their strongest level and pins every dependency to an exact version. A CMake dependency names an exact tag or a commit hash — never a branch, a range, or a floating `latest`.
Enforced by: lint.c.clang-tidy-missing, pin.docker.unpinned-image, pin.ci.unpinned-action
## D-08 — Fail loud
Severity: block
MANDATE: Production code never aborts on recoverable absence. An unchecked `std::optional::value()`, a raw `.at()` on an unproven index, a raw `new` and a raw `delete` are denied; the failure returns `std::expected` and is handled or propagated as a typed domain error.
Enforced by: calisthenics-code.bare-unwrap
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
MANDATE: Every dependency enters by an exact version, and the resolution is committed so a rebuild resolves what the author built. Pin the CMake fetch to an exact tag or commit hash, vendor third-party code under `src/adapters`, and never track a branch.
Enforced by: pin.ci.unpinned-action, pin.docker.unpinned-image

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

## P-05 — macOS first
Severity: block
MANDATE: macOS is the delivery target until the first release ships; Linux follows and Windows comes last. A feature is accepted on macOS before any other target is claimed, and platform-specific code lands behind the platform layer instead of inline.
Enforced by: config:project, verify

## P-06 — C++ and Qt only
Severity: block
MANDATE: The application is C++ compiled against Qt, with no second toolkit and no second language; Qt Widgets is the UI stack and QML enters only through an ADR. The domain layer names no Qt type, so the domain compiles without the framework.
Enforced by: config:project, verify

## P-07 — Architecture model is authoritative
Severity: block
MANDATE: Layers run inward as declared in `architecture.layerOrder` — `domain`, `application`, `ports`, `adapters` — and platform code lives in `src/platform`. An import that breaks the direction moves, or the layer table widens through an accepted ADR.
Enforced by: config:architecture, verify

## P-08 — Fast dev loop, release on authorization
Severity: block
MANDATE: Development builds use the fastest configuration — Ninja, `sccache`, `lld`, `-O0 -g1`, precompiled headers, per-target builds — and the optimized configuration (`-O3`, thin LTO, dSYM, universal binary, strip, notarization) runs only with explicit operator authorization. A dev artifact never ships as release. Builds and tests use every core: one CTest entry per case, `ctest -j` on by default, and no test that quietly serializes the suite.
Enforced by: config:project, verify

## P-09 — Kernel primitive behind a port
Severity: block
MANDATE: The transport reaches the kernel through the `IoEngine` port in `src/ports/io/`, never through `QNetworkAccessManager`; the backend is chosen at startup and no call site names one. A new platform adds an adapter under `src/adapters/platform/io/` and a conformance case, never a branch at the call site.
Enforced by: config:architecture, verify

## P-10 — Six log levels, always declared
Severity: block
MANDATE: Every log call names one of `trace`, `debug`, `info`, `warn`, `error` or `panic`, plus the category of its source directory; `panic` logs and aborts, and an absence that is recoverable never uses it. The level is filtered at runtime by `ANTARES_LOG`, a disabled level costs one guarded branch, and no record carries a token, a certificate or a `kubeconfig`.
Enforced by: config:project, verify

## P-11 — Newest C++ in full, no C escape hatch
Severity: block
MANDATE: The project compiles as C++26, the newest mode the toolchain accepts, and prefers the language's facilities to C — `std::expected`, `std::span`, `std::string_view`, `std::byte`, `std::optional`, `std::variant`, concepts, `constexpr`. A `void*`, a pointer-plus-count pair, a C array and a `#define` constant carry a reason, and a facility the toolchain lacks is gated by its feature-test macro instead of assumed. A duration is `std::chrono::duration`, never a bare millisecond integer; wire bytes are `std::byte`, never `char`. The toolchain floor is CMake 4.4 and the newest Xcode clang, because `CXX_STANDARD 26` needs both. Language features follow the compiler; library features the standard library ships, gated by their feature-test macro. Verified present on the current clang: `= delete("reason")`, structured bindings in a condition, pack indexing, `<expected>`, `<span>`, `<print>`, `<execution>`. Verified absent: `<inplace_vector>`, `<linalg>`, the contracts flag. Nothing on the absent list is assumed.
Enforced by: config:project, verify

## P-12 — Ownership over pointers
Severity: block
MANDATE: A raw pointer is non-owning or `nullptr`; ownership is `std::unique_ptr`, and `std::shared_ptr` only where a lifetime is genuinely shared. Non-owning sequences are `std::span` and `std::string_view`, which never outlive their range, and a copy is avoided by moving — a duplicated buffer is a defect.
Enforced by: config:architecture, verify

## P-13 — Leak gate always on
Severity: block
MANDATE: Every development run is leak-checked — the `asan` target for AddressSanitizer, the `leak` target for the leak pass — and the binary is symbolized with `dsymutil` so a report names frames. No debug build ships without line information, and a leak report is a finding, not background noise. LeakSanitizer exists on Linux only, and `leaks` cannot inspect an AddressSanitizer binary, so on macOS the leak pass runs a sanitizer-free `dev` build.
Enforced by: config:project, verify

## P-14 — mimalloc, pinned, and never beside a sanitizer
Severity: block
MANDATE: Allocation goes through mimalloc, pinned by URL and SHA-256 at the version the ADR names, with `ANTARES_ALLOCATOR=system` as the escape hatch. Call sites keep using `new` and standard containers and never name the allocator. The sanitizer preset builds with `system`, because AddressSanitizer interposes `malloc` and the two cannot both own it.
Enforced by: config:project, verify

## Governance

Changes to this constitution require a recorded Architecture Decision
(MADR) with status `accepted`, a non-empty `deciders` list, and an
`amendments` entry naming that ADR. Trivial corrections (typos,
formatting) may be committed directly. The C++ standard pin and the frozen
`clang-tidy` configuration move only via accepted ADR.

## Maintenance

Last reviewed: 2026-11-09.
