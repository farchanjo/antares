---
id: 01a1c25a-655d-7d53-b841-837cd4b2bc96
number: 003
slug: kubernetes-api-client-over-the-http-client-port
status: implemented
created_at: 2026-11-09T22:07:46.525570625Z
---
# Feature Specification: Kubernetes Api Client Over The Http Client Port

Feature: 003-kubernetes-api-client-over-the-http-client-port
Created: 2026-11-09

## User Stories

- As the desktop app, I want every API server call to go through one port, so the
  UI never builds a path or parses a Status by hand.
- As a developer, I want the request shape and the error mapping covered by
  cases, so a new resource costs a descriptor instead of a code path.
- As an operator, I want a credential to be resolved once per context, so a token
  never appears in a log or a URL.

## Scope

The module is the client for the Kubernetes REST API: path building, options,
patch types, watch framing, Status mapping and credential resolution. It sits on
top of the HTTP client port and adds no transport of its own.

Out of scope: discovery caching policy, informers, the UI, exec/attach over SPDY
(the interface exists, the transport is a later feature).

## Functional Requirements

### FR-001 API path

The client builds `/api/v1/...` for the core group and `/apis/<group>/<version>/...`
for a named group, from a `GroupVersionResource` value. A namespaced resource
carries `/namespaces/<name>` before the resource segment. A subresource appends
`/status`, `/scale`, `/log` or `/exec`. The built path is a value, not a string
built at the call site.

### FR-002 List

A list carries `labelSelector`, `fieldSelector`, `limit` and `continue` as query
parameters, dropped when unset. The continuation token from a response is
returned to the caller unchanged.

### FR-003 Read and write verbs

`get`, `create`, `update`, `patch` and `delete` exist per resource. `patch` names
one of `merge`, `strategic` or `apply` and sets the matching content type, with
`fieldManager` on apply. `delete` carries a propagation policy.

### FR-004 Watch

A watch opens `?watch=true` with `resourceVersion`, `timeoutSeconds` and
`allowWatchBookmarks`, and yields `ADDED`, `MODIFIED`, `DELETED`, `BOOKMARK` and
`ERROR` events. A bookmark advances the stored version without producing a
resource. An `ERROR` event carrying 410 ends the stream with a typed
`expired` so the caller re-lists.

### FR-005 Status mapping

A non-2xx response carrying a `Status` body maps to a typed error with its
reason (`NotFound`, `AlreadyExists`, `Conflict`, `Forbidden`, `Unauthorized`,
`Invalid`, `Timeout`, `TooManyRequests`, `InternalError`, `ServiceUnavailable`).
A body that is not a Status maps to `unexpected` with the code and a bounded
snippet.

### FR-006 Kubeconfig

A kubeconfig document parses into clusters, users and contexts, with the current
context selected. A named context override wins. A missing file, a missing
current context and a context naming an absent cluster or user each produce a
distinct typed error.

### FR-007 Credentials

A credential is resolved from the selected user into one of: bearer token, client
certificate, exec plugin output, or none. The value is rendered into headers or
TLS material at request time and never into the URL.

### FR-008 Pod log stream

A log request streams lines from `pods/log` and honours `container`,
`follow`, `tailLines`, `sinceSeconds` and `timestamps`. `follow` keeps the stream
open until the peer ends it or the caller cancels.

### FR-009 Exec interface

`exec` accepts a command, a container and a TTY flag and returns a session value
that can write stdin, read stdout/stderr and resize. The transport behind it is
declared, not implemented, in this feature.

### FR-010 Discovery

`apiGroups` and `serverResources` parse discovery documents into group, version
and resource descriptors, including whether a resource is namespaced and which
verbs it allows.

### FR-011 Scale

`scale` reads and writes the `scale` subresource for a workload, carrying
`replicas` and the observed selector.

### FR-012 Idempotent retry

`get`, `list` and `watch` retry on `429` and `503` honouring `Retry-After`; a
write retries only when the caller marked it idempotent. The retry count is
bounded by the client configuration.

## Security Requirements

- **Data sensitivity/classification.** Secrets flow through the client as opaque
  JSON; the module never decodes base64 payloads. A token and a client key are
  the sensitive values it holds in memory.
- **Authentication/authorization.** Every request carries exactly one credential
  resolved from the selected kubeconfig user. The API server does the
  authorization; the client adds no policy of its own.
- **Input validation.** Resource names, namespaces and group versions are
  validated against the DNS-1123 label rules before they reach a path, so a
  hostile name cannot traverse the URL. Selectors are percent-encoded. Bodies
  are bounded by the configured response ceiling.
- **Cryptography in transit/at rest.** Transport is TLS through the HTTP client;
  the module adds no cipher of its own. Certificate authority data from the
  kubeconfig feeds the trust store, and `insecureSkipTLSVerify` is honoured.
- **Logging/audit.** The method, the path and the resulting status are logged at
  `debug`; the credential is not. A retry logs at `debug` with the attempt
  number.
- **Error-handling information exposure.** A mapped Status carries the reason and
  the server message, bounded to 512 bytes, and never the request headers.

## Acceptance Scenarios

Given a kubeconfig with one context
When  a namespaced list is submitted
Then  the path is `/api/v1/namespaces/<ns>/pods` and the selectors ride the query

Given a 404 Status body
When  a `get` runs
Then  the error is `notFound`, not a generic transport failure

Given a watch stream
When  a `BOOKMARK` event arrives
Then  the stored resource version advances and no resource is emitted

## Observability

`KubeClientStats` carries `submitted`, `mappedStatus`, `retried` and
`watchEvents` per client. Labels stay bounded: the outcome, and whether the
target was namespaced. Logs use the `k8s` category. A span would name the verb
and the resource kind, never the namespace name.
