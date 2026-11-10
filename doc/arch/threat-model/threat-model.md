# Threat Model (STRIDE)

The sibling of `privacy-threat-model.md`. STRIDE asks the attack question; the
LINDDUN document asks the privacy one, and each threat below has its privacy
twin there.

The trust boundary is the wire. Everything inside the process is ours; the API
server, the proxy and the name resolver are not. A threat enters here only if
it crosses that boundary, and the mitigation is named as the code path that
stops it, not as an intention.

`speckit validate` scans the table below. The `Category` column uses the six
lowercase STRIDE tokens exactly.

| ID | Category | Threat | Boundary crossing | Mitigation |
|----|----------|--------|-------------------|------------|
| TM-01 | spoofing | a forged server answers the connection | name resolution, TCP accept | `SSL_CTX_set_verify` runs with `SSL_VERIFY_PEER` and the default trust paths (`src/adapters/http/tls_context.cpp`); ALPN is decided from the server list, never assumed |
| TM-02 | spoofing | a redirect bounces the client to an origin it does not trust | 3xx response | the hop count is bounded by `Limits::maxRedirects` (`src/domain/http/limits.hpp`) and the credential is dropped on a cross-origin hop unless `forwardAuth` is set |
| TM-03 | tampering | a middlebox rewrites the ciphertext | TLS record layer | TLS 1.2 is the floor and the negotiated version is read back, never presumed; h2 frames are length-checked before a payload is sized |
| TM-04 | tampering | a chunked or h2 body drives an allocation past memory | response body | `Limits::maxBodyBytes` and `Limits::maxHeaderBytes` cap every buffer a peer chooses the size of |
| TM-05 | tampering | a mangled kubeconfig points at an attacker cluster | `KUBECONFIG` file | the document is parsed into typed values (`src/adapters/k8s/kubeconfig_loader.cpp`); a context naming an absent cluster is a typed error, not a silent default |
| TM-06 | repudiation | the operator cannot prove which call the app made | outbound request | method and URL are logged at `trace`; the body is not, so a secret never reaches a log sink |
| TM-07 | repudiation | a watch stream is replayed and looks like new events | watch framing | the resource version rides every event and the caller sees the bookmark, so a resume is provable |
| TM-08 | information-disclosure | a bearer token lands in a URL or a log line | request line | the credential is resolved once per context and travels in a header; only the auth kind is logged |
| TM-09 | information-disclosure | a pooled connection keeps a stale identity | connection reuse | a connection is bound to the authority and the credentials that opened it; a different authority opens a different connection |
| TM-10 | information-disclosure | an error message leaks a host or a path | failure path | `IoError` carries the `errno` value and not a formatted system string; the Kubernetes `Status` is mapped to a typed error, not echoed |
| TM-11 | denial-of-service | an idle peer pins a descriptor forever | pool | per-host idle ceiling and a read deadline (`src/domain/http/timeouts.hpp`); the idle timer closes the socket, it does not park it |
| TM-12 | denial-of-service | one connection starves the rest | h2 multiplexing | `Limits::maxStreamsPerConnection` bounds the concurrency of one connection, so a single peer cannot exhaust the pool |
| TM-13 | denial-of-service | a name lookup blocks the loop | DNS | resolution runs off the loop thread and is bounded by a watchdog; the loop never waits on a resolver |
| TM-14 | elevation-of-privilege | a kubeconfig `exec` block runs arbitrary code | credential plugin | the plugin is invoked with the declared arguments and its output is parsed as a credential document, never evaluated as shell |
| TM-15 | elevation-of-privilege | a cluster-scoped call is made on a namespaced path and hits the wrong object | path build | the path is built from a `GroupVersionResource` value plus an optional namespace (`src/domain/k8s/api_path.cpp`); the caller never concatenates a segment |
| TM-16 | elevation-of-privilege | a write is retried and applies twice | retry policy | a write retries only when marked idempotent, and a read retries only on a retryable status |

## Residual risk

A hostile API server is trusted to describe itself: discovery, `Status` reasons
and scale subresources are believed once TLS holds. A server that lies about its
own resources produces wrong paths, not a compromised client.

## Out of scope

RBAC, admission control and Secret encryption at rest belong to the API server.
The app holds whatever credential the kubeconfig gave it and adds no privilege
of its own.
