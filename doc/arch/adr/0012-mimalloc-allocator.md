---
status: accepted
date: 2026-11-09
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0012 — mimalloc as the allocator

## Context and Problem Statement

The engine allocates per operation: a read buffer per `submitRead`, a node per
pending operation, a string per log record. The system allocator is the wrong
tool for that shape. On Darwin it is also the wrong tool for a portable
application: allocation cost differs per OS, and a measured trace on one platform
says nothing about the other.

An allocator is one of the few dependencies that touches every allocation in the
process, so it is chosen once and recorded.

## Decision Drivers

- Allocation sits on the hot path of every completion.
- GPL-3.0-only project: the licence must be compatible.
- macOS arm64 is the acceptance target (P-05); a Linux-only allocator is out.
- AddressSanitizer replaces `malloc`, so the two cannot both own it (P-13).

## Considered Options

Measured against the GitHub API on 2026-11-09:

| Allocator | Licence | Last push | Platforms |
|---|---|---|---|
| mimalloc | MIT | 2026-11-09 | Windows, macOS, Linux |
| jemalloc | BSD-2-Clause | 2026-11-07 | POSIX, macOS best-effort |
| tcmalloc | Apache-2.0 | 2026-11-09 | Linux only |
| snmalloc | MIT | 2026-11-08 | narrow platform set |
| rpmalloc | MIT | 2026-10-21 | unmaintained cadence |

- **mimalloc** — MIT, explicit macOS and Apple arm64 work, `mimalloc-override.h`
  plus `mimalloc-new-delete.h` for C++ `operator new`, documented ASan tracking
  mode. Chosen.
- **jemalloc** — the classic answer, and it ships in Redis. But its macOS
  support is best-effort: open issues for a Sonoma test failure, a segfault
  reported through apache-arrow, and heap profiling that does not work on macOS.
- **tcmalloc** — its own platform guide lists Linux only, so it fails P-05.
- **snmalloc, rpmalloc** — plausible, less macOS mileage than mimalloc.

## Decision Outcome

Chosen option: **mimalloc, pinned to `v3.6.1` by URL and SHA-256, enabled by
default outside sanitizer builds.**

`ANTARES_ALLOCATOR` selects `mimalloc` (default) or `system`. The sanitizer
preset sets `system`, because ASan interposes `malloc` itself and the two cannot
both own it.

`mimalloc-override.h` and `mimalloc-new-delete.h` are included once, in a
dedicated translation unit, so C++ `operator new`/`delete` are routed too. No
call site changes: everything keeps calling `new`, `std::vector` and
`std::make_unique`.

The fetch is a URL with `URL_HASH SHA256=2672e6a9…`, not a moving tag, so a
rebuild resolves the bytes this record was written against (D-17).

### Consequences

- Good: the hot path stops paying the system allocator.
- Good: MIT, compatible with GPL-3.0-only.
- Good: no call-site change, so the port stays allocator-agnostic.
- Bad: one more dependency in the link, and a static runtime that must be
  released with the application.
- Bad: the ASan build exercises a different allocator than the shipped build, so
  an allocator bug can hide behind the sanitizer preset.
- Bad: mimalloc's own debugging state (`MI_OPTION_VERBOSE`, `mi_stats`) is a new
  surface to learn.

### Confirmation

`ANTARES_ALLOCATOR=system` builds and passes the suite, and so does the default;
`dev-asan` builds with `system` and stays green.
