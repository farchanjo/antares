# Privacy Threat Model (LINDDUN)

antares holds no account database. What it touches is the credential to a
cluster and the traffic to it, so the privacy surface is narrow and the
interesting threats are leakage of that credential and inference from timing.

LINDDUN asks "how can the system harm the privacy of a person?"; the sibling
STRIDE model asks the attack question. The seven categories are Linking,
Identifying, Non-repudiation, Detecting, Data-disclosure, Unawareness and
Non-compliance.

`speckit validate` scans the table below; the `Category` column uses the seven
lowercase tokens exactly.

| ID | Category | Threat | Affected Data | Mitigation |
|----|----------|--------|---------------|------------|
| PT-01 | linking | a redirect forwards the credential to another origin | `Authorization` header | dropped on cross-origin hops unless `forwardAuth` is set |
| PT-02 | identifying | the credential appears in a log line | bearer token, basic password | only the auth kind is logged, never the material |
| PT-03 | non-repudiation | an operator cannot deny a call the app made | request history | the method and URL are logged at `trace`, the body is not |
| PT-04 | detecting | response timing reveals whether a host exists | connection latency | name resolution is off the loop, so a slow name is not a timing oracle for the caller |
| PT-05 | data-disclosure | plaintext leaks to a downgrade | wire bytes | ALPN result is explicit and TLS 1.2 is the floor |
| PT-06 | unawareness | the operator does not know a proxy is trusted | trust store | a supplied bundle replaces the system store rather than adding to it |
| PT-07 | non-compliance | retained traffic outlives its purpose | pooled connections | idle sockets are closed at the configured ceiling |

## Out of scope

Cluster-side audit, RBAC and Secret storage belong to the API server the app
talks to, not to this client.
