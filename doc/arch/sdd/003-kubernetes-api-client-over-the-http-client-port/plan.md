# Plan: Kubernetes API client

## Summary

Add the client for the Kubernetes REST API as a module over the existing HTTP
client port. The module owns the request shape, the Status mapping, the watch
framing and the credential resolution; the transport stays where it is.

## Layer placement

- `src/domain/k8s/` — values and codecs, no I/O: `GroupVersionResource`,
  `ApiPath`, `ListOptions`, `PatchType`, `DeleteOptions`, `Status`, `ApiError`,
  `WatchEvent`, `Kubeconfig`, `Credential`, `Scale`.
- `src/ports/k8s/` — `KubeClient`, `Discovery`, `CredentialSource`.
- `src/adapters/k8s/` — `HttpKubeClient`, `HttpDiscovery`, `KubeconfigLoader`.

Dependencies point inward: the adapter sees the port and the domain, the port
sees the domain, the domain sees nothing.

## Constitution

- D-01: spec first — this plan follows `spec.md`, and the cases follow the FR ids.
- D-03: the domain holds no I/O; the adapter is the only layer that touches the
  HTTP client.
- D-05: primitives are wrapped — a resource is not a `std::string`, a version is
  not an `int`.
- D-06: includes spell the path from `src/`.
- D-08: every fallible step returns `std::expected`.
- D-11: the port surface is additive; a verb is added, never reshaped.
- D-15: `KubeClientStats` counts, and the labels stay bounded.

## Domain model

`GroupVersionResource` is the identity: group (empty for core), version, resource.
Validation is DNS-1123, so a hostile name cannot traverse the path.
`ApiPath` renders it, plus the namespace and the subresource.

`Status` is the server's error document; `ApiError` is its typed projection.
`WatchEvent` is the stream frame.

`Kubeconfig` is a document of clusters, users and contexts, with the current
context selected. `Credential` is the resolved user.

## Sequence: one request

1. The caller names a `GroupVersionResource`, an option set and a body.
2. The adapter builds the path, the query and the headers, then asks the
   `CredentialSource` for the credential of the active context.
3. The request goes to `ports::http::Client` bound to the cluster authority.
4. A 2xx body decodes into the caller's type; a non-2xx body decodes into
   `Status` and becomes an `ApiError`.

## Sequence: a watch

Frames arrive on a streaming sink. A `BOOKMARK` advances the stored version and
emits nothing. An `ERROR` frame carrying 410 ends the stream with `expired`.

## Testing

One case per FR against the loopback server: the request line, the query, the
headers, the mapped error, the watch frame order. A kubeconfig fixture covers
each failure mode.

## Risks

- The kubeconfig parser is a YAML subset; a document outside the subset fails
  with a typed error rather than misparsing silently.
- Exec/attach needs a framed protocol (SPDY or WebSocket); the interface lands
  here and the transport is a later feature.
