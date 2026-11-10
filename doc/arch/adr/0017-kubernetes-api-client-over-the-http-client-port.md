---
status: accepted
date: 2026-11-09
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0017 — Kubernetes API client over the HTTP client port

## Context and Problem Statement

antares manages clusters, so it speaks the Kubernetes REST API. That API is
versioned, grouped, namespaced and error-typed: a path is
`/apis/<group>/<version>/namespaces/<ns>/<resource>/<name>/<subresource>`, a
failure is a `Status` document with a machine-readable reason, and a watch is a
streamed frame sequence carrying `resourceVersion`.

Writing that shape at each call site spreads the same four concerns — path,
options, Status and watch framing — across the UI. It also makes the credential
easy to leak into a URL.

## Decision Drivers

- The HTTP client already exists and owns the transport; the module must not
  duplicate it.
- A wrong path is a silent 404 at runtime, so the path deserves a value type.
- A `Status` reason is the difference between "retry" and "show a dialog".
- The credential is the most sensitive value the app holds.

## Considered Options

- **A generated client from the OpenAPI spec.** Complete, but the generator is a
  second toolchain and the output is large and repetitive.
- **A thin wrapper over `kubectl`.** No types, no streaming, an external binary.
- **A hand-written module over the HTTP client port.** Smaller, typed, and the
  surface is exactly what the UI needs.

## Decision Outcome

Chosen option: **a hand-written module over the HTTP client port.**

It splits into a pure domain (path, options, Status, watch frame, kubeconfig,
credential), a port (`KubeClient`, `Discovery`, `CredentialSource`) and one
adapter that builds requests. The transport is untouched.

### Consequences

- Good: a new resource is a descriptor, not a code path.
- Good: the credential is rendered in one place and never reaches a URL.
- Good: path and Status mapping are unit-testable without a server.
- Bad: the surface is hand-maintained, so a new API verb needs a deliberate edit.
- Bad: the kubeconfig parser is a YAML subset and refuses what it does not know.

## Pros and Cons of the Options

### Generated client

Buys completeness, costs a generator in the build and a large diff per upgrade.
Reconsider when the resource set outgrows a descriptor table.

### `kubectl` wrapper

Cheapest start, but the app inherits a binary, its version and its output
format, and loses streaming control.

## More Information

Related: ADR-0013 (HTTP client), ADR-0006 (I/O port). Spec:
`doc/arch/sdd/003-kubernetes-api-client-over-the-http-client-port/spec.md`.
