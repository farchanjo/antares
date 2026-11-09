---
status: accepted
date: 2026-11-09
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0001 — Single source root under `src/`

## Context and Problem Statement

antares is a cross-platform desktop application with three target operating
systems and a build that will grow CMake targets, platform-specific sources and
test trees. Without a declared root, source files drift to the repository root,
to ad-hoc directories, and next to documentation, and every tool that scans
source needs its own path list.

The layer and lint scanners read `architecture.corePaths`. When that list is
empty, no source-level rule can fire and the constitution cannot gate anything.

## Decision Drivers

- One predictable place to find production code.
- Scanners (layers, lint, calisthenics) need a declared root to inspect.
- Platform-specific code must sit in a named subdirectory, not at the root.
- The project root stays for build entry points and repository metadata.

## Considered Options

- **Source under `src/`** — one declared root; the configuration names it.
- **Source at the repository root** — flat, but the root fills with sources and
  every scanner needs a broader include list.
- **Source under `code/`** — equivalent shape, less conventional for CMake.

## Decision Outcome

Chosen option: **source under `src/`**.

Every production source file lives under `src/` at the project root.
`architecture.corePaths` names `src`, so the layer and lint scanners read the
same tree the rule describes. No source file sits at the root, under `doc/`, or
in an ad-hoc directory.

### Consequences

- Good: one root, one configuration entry, scanners have a target.
- Good: platform subdirectories (`src/platform/…`) are named explicitly.
- Bad: build files need one extra path segment in every source list.
- Bad: a misplaced file is a validation finding, not a style opinion.

### Confirmation

`speckit validate` reports a finding when a scanned rule resolves outside
`architecture.corePaths`, and the constitution carries the rule as `P-04`.
