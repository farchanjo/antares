# Service Level Objectives

antares is a desktop client, so the objectives are local latency and
correctness, not availability of a server.

| Objective | Target | Measured by |
|-----------|--------|-------------|
| Idle costs no CPU | 0 wakeups while nothing is pending | `kevent` blocks with no timeout |
| A queued task never waits | runs on the next loop turn | `tasks_` short-circuits the wait to 0 |
| Retry is bounded | `maxAttempts` reached, then the last response | `ClientStats::retries` stops climbing |
| Streams do not buffer unbounded | chunks delivered per read | sink called per chunk, body not accumulated |
| Cancellation is prompt | wire released on the same turn | connection closed or `RST_STREAM` sent |

## Error budget

A retry that fires on a non-idempotent method without `retryNonIdempotent` is a
defect, not a slow path: it can duplicate work. The same applies to a completion
that runs after `stop()`.

## Not covered

Availability of a remote cluster, and throughput ceilings. Both depend on the
peer and belong to a load test, not to this document.

## Review

Review these objectives when a transport decision changes: a new backend, a new
timeout, or a new retry condition. A target that no longer has a test is
removed rather than documented as aspirational.

## Burn

There is no rolling error budget: the suite is the budget. A red case spends it
immediately, and a case that passes only on retry is treated as red.
