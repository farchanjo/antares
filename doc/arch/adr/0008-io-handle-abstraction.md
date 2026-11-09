---
status: accepted
date: 2026-11-09
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0008 — One handle type for files, pipes and sockets

## Context and Problem Statement

The port as first written took a bare `int`. That pushes the descriptor kind
onto every caller: a regular file cannot be waited on the same way a socket can,
so a caller that does not classify will wait forever or spin.

Measured on macOS 27 with kqueue: a regular file reports `EVFILT_READ` ready
immediately, on every sweep, with `data` equal to the file size. A readiness
loop over an open file therefore burns CPU at 100% while making no progress.

The system must not change the I/O path per descriptor kind. The engine has to
absorb the difference.

## Decision Drivers

- A caller opens a thing and reads it; the kind is an implementation detail.
- Classification is one `fstat` and belongs where the descriptor is adopted.
- Files need positional I/O; streams need sequential I/O.
- The conformance suite must cover both strategies through one interface.

## Considered Options

- **`IoHandle` value type, classified at adoption** — one API, engine decides.
- **Bare `int` plus `HandleKind` parameter** — the caller classifies and can
  lie; two places to keep in sync.
- **Separate file and stream APIs** — honest, but doubles the surface and moves
  the branch into the caller.

## Decision Outcome

Chosen option: **`IoHandle`, an owning move-only descriptor classified once by
`fstat` at adoption.**

`IoHandle::adopt` and `IoHandle::openFile` return `std::expected<IoHandle,
IoError>`. A destroyed handle closes its descriptor. `release()` gives the
descriptor back for `exec`.

`HandleKind` has three cases: `stream` (socket, FIFO, character device), `file`
(regular file) and `other`. Port operations take `const IoHandle &` and an
optional offset.

The engine selects the strategy from the kind:

- `stream` — register the readiness filter, wait, complete once.
- `file` — do not register. Post a task that performs `pread`/`pwrite` at the
  given offset and completes. `offset` is required here and ignored there.

`other` takes the stream strategy. A file reported `closed` at offset equal to
its size is end-of-file, not an error.

### Consequences

- Good: callers never branch on descriptor kind.
- Good: ownership is RAII, so a lost path cannot leak a descriptor.
- Good: the conformance suite drives both strategies through one interface.
- Bad: `offset` is optional and therefore misusable on a stream.
- Bad: `adopt` runs `fstat` on every descriptor, including hot accept paths.
- Bad: a handle must outlive its in-flight operations; the engine stores a
  pointer and cannot prove it.

### Confirmation

`src/ports/io/io_handle.hpp` classifies on adoption, the port takes
`const IoHandle &`, and the suite covers a file read and a socket read through
the same call.
