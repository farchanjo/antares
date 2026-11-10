---
status: accepted
date: 2026-11-09
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0018 — io_uring first on Linux, epoll as the fallback

## Context and Problem Statement

ADR-0007 made kqueue the engine and ADR-0009 kept `poll` as the escape hatch.
Neither runs on Linux: kqueue is a BSD interface, and `poll` scans the whole
descriptor set on every wait, so it degrades exactly where a cluster view is
busiest.

Linux has two native readiness interfaces: `epoll`, which is the direct
counterpart of kqueue, and `io_uring`, which batches submissions and completions
through shared rings instead of a syscall per change.

## Decision Drivers

- The transport must not be the reason a platform is slow.
- A kernel interface can be unavailable (old kernel, seccomp, `io_uring_disabled`),
  so a fallback is mandatory, not optional.
- Three backends must not mean three behaviours: a case that passes on one must
  pass on all of them.

## Considered Options

- **epoll only.** Simple, and the counterpart of what already exists. Costs the
  ring, which is measurably cheaper per operation.
- **io_uring only.** Fastest, but a hardened container can refuse it and leave
  the app with nothing.
- **io_uring, then epoll, then poll.** Every kernel gets the best interface it
  offers, and the environment can still pin one.

## Decision Outcome

Chosen option: **io_uring, then epoll, then poll.**

The engine factory tries the ring, falls back to epoll when the kernel refuses
it, and falls back to `poll` when neither is available. `ANTARES_IO_BACKEND`
pins a backend, which is also how the suite exercises all three.

The new engine reports readiness rather than results, like the other two: the
poll fires and the shared base performs the syscall. That is what keeps one
conformance suite meaningful across the three.

### Consequences

- Good: Linux gets the cheapest interface it can offer.
- Good: a refused ring is a fallback, never a failed run.
- Good: the suite covers all three backends, so they cannot drift silently.
- Bad: `io_uring` is completion-shaped, so its one-shot polls need rearming and
  a bounded sleep; both are documented at the call site.
- Bad: a third backend is a third place for a readiness bug to hide.

## Pros and Cons of the Options

### epoll only

Less code, but leaves the ring unused on kernels that have had it since 5.1.
Reconsider only if the ring proves unstable in practice.

### io_uring only

Fastest and simplest factory. A `seccomp` profile without `io_uring_setup`
turns the app into a `poll`-only build without saying so.

## More Information

Related: ADR-0006 (the engine is a port), ADR-0007 (kqueue), ADR-0009 (forced
kqueue with a `poll` escape). The Linux engine lives in
`src/adapters/platform/io/io_uring_engine.cpp`.
