# Tasks: Kubernetes API client

## Task Breakdown

- [x] T001 Domain: `GroupVersionResource` with DNS-1123 validation and a path builder
      that emits `/api/v1` for the core group and `/apis/<g>/<v>` otherwise.
- [x] T002 Domain: `ListOptions` (label, field, limit, continue) rendered as a query
      string, dropped when unset, percent-encoded when set.
- [x] T003 Domain: `PatchType` (merge, strategic, apply) with its content type, and
      `DeleteOptions` with a propagation policy.
- [x] T004 Domain: `Status` decoding and mapping to `ApiError` reasons, with the
      server message bounded to 512 bytes.
- [x] T005 Domain: `WatchEvent` decoding for ADDED/MODIFIED/DELETED/BOOKMARK/ERROR.
- [x] T006 Domain: kubeconfig model and parser — clusters, users, contexts, current
      context, with a typed error per failure mode.
- [x] T007 Domain: `Credential` rendering — bearer, client certificate, exec output,
      none.
- [x] T008 Port: `KubeClient` with get, list, create, update, patch, delete, watch,
      logs, scale and exec.
- [x] T009 Port: `Discovery` for api groups and server resources.
- [x] T010 Port: `CredentialSource` resolving a credential for a context.
- [x] T011 Adapter: request building over `ports::http::Client`, including headers,
      content type and query.
- [x] T012 Adapter: response handling — status mapping, list decode, watch stream.
- [x] T013 Adapter: kubeconfig loader from a path or `KUBECONFIG`.
- [x] T014 Cases: path building, options, status mapping, watch framing, kubeconfig
      failures, credentials.
- [x] T015 Wire the module into CMake and the conformance binary.

## Dependencies

The HTTP client port (feature 002) and the logging port. No new third-party
dependency: the kubeconfig parser is a subset written here.
