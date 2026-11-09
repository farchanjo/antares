# Observability Strategy

antares is a desktop client, so it is observed by its own operator on the
machine it runs on: a stalled loop and a failing handshake must be visible
without a debugger attached.

## Signals

Three families exist on paper: metrics, logs and traces. Only the first two are
emitted today.

## Metrics

Counters only, and only where the label set stays bounded.
`EngineStats` carries `submitted`, `completed` and `failed` per backend.
`ClientStats` is a snapshot, never a label carrier; the stream count per
connection is a gauge.

## Logs

One line per event, level and category first: the category is the source
directory (`http`, `io`). The credential is never logged, only its kind. A TLS
failure logs at `error`, an idle timeout at `warn`, a retry at `debug` and a
redirect at `trace`.

## Tracing

There is no collector yet. A span would name the operation (`GET /pods`) and
carry the attempt number and the negotiated protocol as attributes; the trace id
would travel in the request so a server-side log can be joined.

## Cardinality

| Label | Bounded Value Set | Signal |
|-------|-------------------|--------|
| `http.version` | `h2`, `http/1.1` | request metrics |
| `http.outcome` | `ok`, `connect`, `tls`, `timeout`, `cancelled`, `refused`, `protocol` | request metrics |
| `pool.state` | `opened`, `reused`, `idle_closed` | connection metrics |

Never a label: `user_id`, `request_id`, `session_id`, `email`, `uuid`, or the
URL path. Cluster names and namespaces are bounded by the operator's own config
and are acceptable as labels once a metrics sink exists.

## Export

None of this is exported today; the counters are read in-process and printed by
the test binary on failure. OTLP over gRPC 4317 or HTTP 4318 is the intended
transport when a sink exists, with `service.name = "antares"`.
