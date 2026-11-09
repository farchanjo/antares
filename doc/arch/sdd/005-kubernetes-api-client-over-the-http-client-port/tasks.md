# Tasks: Kubernetes API client

## Task Breakdown

- [ ] T001 Domain: `GroupVersionResource` with DNS-1123 validation and a path builder
      that emits `/api/v1` for the core group and `/apis/<g>/<v>` otherwise.
- [ ] T002 Domain: `ListOptions` (label, field, limit, continue) rendered as a query
      string, dropped when unset, percent-encoded when set.
- [ ] T003 Domain: `PatchType` (merge, strategic, apply) with its content type, and
      `DeleteOptions` with a propagation policy.
- [ ] T004 Domain: `Status` decoding and mapping to `ApiError` reasons, with the
      server message bounded to 512 bytes.
- [ ] T005 Domain: `WatchEvent` decoding for ADDED/MODIFIED/DELETED/BOOKMARK/ERROR.
- [ ] T006 Domain: kubeconfig model and parser — clusters, users, contexts, current
      context, with a typed error per failure mode.
- [ ] T007 Domain: `Credential` rendering — bearer, client certificate, exec output,
      none.
- [ ] T008 Port: `KubeClient` with get, list, create, update, patch, delete, watch,
      logs, scale and exec.
- [ ] T009 Port: `Discovery` for api groups and server resources.
- [ ] T010 Port: `CredentialSource` resolving a credential for a context.
- [ ] T011 Adapter: request building over `ports::http::Client`, including headers,
      content type and query.
- [ ] T012 Adapter: response handling — status mapping, list decode, watch stream.
- [ ] T013 Adapter: kubeconfig loader from a path or `KUBECONFIG`.
- [ ] T014 Cases: path building, options, status mapping, watch framing, kubeconfig
      failures, credentials.
- [ ] T015 Wire the module into CMake and the conformance binary.

## Dependencies

The HTTP client port (feature 002) and the logging port. No new third-party
dependency: the kubeconfig parser is a subset written here.
