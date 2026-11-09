---
id: 01a1c19f-2ef2-7052-b90a-6cb570c7b6f9
number: 001
slug: kernel-io-engine-kqueue
status: implemented
created_at: 2026-11-09T18:43:17.362081801Z
---
# Feature Specification: Kernel Io Engine Kqueue

Feature: 001-kernel-io-engine-kqueue
Created: 2026-11-09

## User Stories

- As the transport layer, I want one completion-oriented interface over the
  kernel primitive so that the HTTP client does not learn a platform.
- As an operator on macOS, I want kqueue used natively so that a thousand idle
  connections cost one syscall, not a thousand.
- As a reviewer, I want a portable `poll` backend beside `kqueue` so that a
  conformance suite can compare both on the same cases.

## Functional Requirements

### FR-001 Completion-oriented port

`IoEngine` in `src/ports/io/` declares `submitRead`, `submitWrite`,
`submitAccept`, `submitConnect`, `post`, `run` and `stop`. Every operation takes
a completion callback. No operation blocks the caller.

### FR-002 Buffer ownership

`submitRead` accepts a `std::vector<std::byte>` by value and returns the filled
buffer to the callback. The caller never retains a pointer into engine memory,
so a completion after caller scope exit stays valid.

### FR-003 kqueue backend

On Darwin the factory returns the kqueue backend. Registration of a descriptor
costs O(1) `kevent` calls; a readiness sweep does not scan unregistered
descriptors.

### FR-004 Backend selection

The factory picks a backend at startup and the choice is observable through
`IoEngine::name()`. Darwin selects `kqueue`; a build without kqueue support
selects `poll`. No call site names a backend.

### FR-005 Typed failure

Every operation returns `std::expected<void, IoError>` and no exception crosses
the port boundary. `IoError` is an enum class with a case per failure family
(`wouldBlock`, `closed`, `cancelled`, `tooManyDescriptors`, `system`).

### FR-006 Cancellation

Cancelling a pending operation invokes its callback exactly once with
`IoError::cancelled`. After `stop()` returns, no callback runs.

### FR-007 Single engine thread

Callbacks run on the engine thread. `post()` enqueues a callable onto that same
thread. A callback never executes on the submitting thread.

### FR-008 Conformance suite

One suite exercises every backend through the port only. `kqueue` and `poll`
both pass it. Adding a backend means adding a case, not editing the suite.

## Security Requirements

- **Data sensitivity/classification.** The engine moves opaque bytes between a
  descriptor and a caller buffer. It classifies nothing and parses nothing; TLS
  and HTTP sit above it.
- **Authentication/authorization.** None at this layer. The engine holds
  descriptors the caller opened and never decides access.
- **Input validation.** Descriptor numbers and buffer sizes are the untrusted
  input. A closed or foreign descriptor is rejected at registration; a zero-size
  read is rejected before submission.
- **Cryptography in transit/at rest.** None here. The port carries ciphertext
  without inspection once TLS is above it.
- **Logging/audit.** The engine logs backend name, registration count and error
  family. It never logs buffer contents.
- **Error-handling information exposure.** `IoError` carries the `errno` value
  but not a formatted system string, so a message cannot leak a path or host.

## Acceptance Scenarios

Given a built engine
When  a descriptor becomes readable
Then  the read completion fires once with the bytes.

Given a pending operation
When  it is cancelled
Then  the callback fires once with `cancelled` and never again.

Given a running engine
When  `stop()` returns
Then  no further callback runs.

The executable form lives in
`doc/arch/specs/features/kernel-io-engine-kqueue.feature`.

## Observability

The engine exposes three counters through the port: operations submitted,
operations completed and operations failed per `IoError` family. Counters are
plain integers read on the engine thread.

Log events at `debug`: backend selected at startup, descriptor registered and
descriptor unregistered. Log at `warn`: submission rejected because the queue is
full.

No trace span is created here; spans belong to the HTTP layer above. The label
set stays bounded because the backend name is the only label.

## Clarifications
