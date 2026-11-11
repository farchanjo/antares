# Quality Scenarios

Quality here is measured at the transport: antares lives or dies on whether the
engine keeps its loop free and whether the client tells the truth about what the
peer did. Each row is an ATAM-style scenario mapped onto an ISO/IEC 25010:2023
characteristic.

The product the scenarios constrain is `doc/arch/functional/product-overview.md`.

The `Attribute` column uses the ISO/IEC 25010:2023 names exactly.
`speckit validate` scans the table below (never the prose around it).

| ID | Attribute | Stimulus | Environment | Response | Measure |
|----|-----------|----------|-------------|----------|---------|
| QS-01 | functional-suitability | a request is submitted | any supported backend | it reaches the peer and the response returns | every conformance case passes on kqueue and poll |
| QS-02 | performance-efficiency | nothing is pending | idle engine | the loop blocks in the kernel | zero wakeups per second while idle |
| QS-03 | compatibility | a peer offers only HTTP/1.1 | TLS with ALPN | the connection still serves | the h1 fallback case passes |
| QS-04 | interaction-capability | a caller blocks on the sync facade | the caller is not the loop | the call returns instead of deadlocking | `reentrant` is reported from the loop thread |
| QS-05 | reliability | a peer answers 503 twice | retry configured | the third attempt succeeds | the retry count is exactly two |
| QS-06 | security | an unknown CA signs the peer | `verifyPeer` on | the handshake fails | the failure is reported as `tls` |
| QS-07 | maintainability | a port gains a method | the suite runs | a case reaches it | no virtual is left uncalled |
| QS-08 | flexibility | the backend is switched | `ANTARES_IO_BACKEND` set | the same suite passes | both backends green from one build |
| QS-09 | safety | a peer sends an oversized header block | engine ceiling set | the read is refused | `headerCeiling` is reported, not a crash |
| QS-10 | performance-efficiency | a scope holds 5 000 fixture pods | the store keeps them | memory stays bounded | at most 2 560 000 bytes allocated for the scope, no admitted entry above 1 024 bytes, the store at most 104 857 600 bytes at its ceilings |
| QS-11 | performance-efficiency | a watch delivers a burst of frames | one engine turn | the turn returns before the paint starves | no completion exceeds the read chunk, and the turn yields between completions |

## Non-functional gates

`asan` and `leak` run on every change; a leak fails the build. A case that
passes only on retry is treated as red.
