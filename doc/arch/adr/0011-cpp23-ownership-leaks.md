---
status: accepted
date: 2026-11-09
deciders: [Fabricio Archanjo]
consulted: []
informed: []
---

# 0011 — Newest C++ in full, ownership over pointers, leak gate on

## Context and Problem Statement

The port already declares C++23, but the tree carries C habits the standard now
covers: `submitConnect` takes `const void *` plus a `std::uint32_t` length, a
pair that can disagree. A `void*` erases the element type, and an integer length
erases the bound.

Leaks matter twice over here. They cost memory in a long-lived engine, and they
poison LLVM tooling: ASan's report is the first thing a debugger shows, and a
noisy report hides the real fault.

## Decision Drivers

- C++23 ships the vocabulary the code needs: `std::expected`, `std::span`,
  `std::string_view`, `std::byte`, `std::optional`, `std::variant`
  ([C++23](https://en.cppreference.com/w/cpp/23)).
- A pointer plus a length is two facts that drift; a view is one.
- RAII is the language's own answer to leaks ([RAII](https://en.cppreference.com/w/cpp/language/raii)).
- LeakSanitizer runs by default on Linux. On Darwin it is absent, and `leaks`
  refuses an AddressSanitizer binary ("doesn't work with memory analysis tools"),
  so a macOS leak pass needs a sanitizer-free build
  ([ASan](https://clang.llvm.org/docs/AddressSanitizer.html)).

## Considered Options

- **C++23 idioms everywhere, views for non-owning data** — typed and bounded.
- **Keep `void*` plus length** — no type, no bound.
- **`shared_ptr` as the default owner** — shared atomic cost on every copy.
- **Leak check only on demand** — the run that would leak is the run skipped.

## Decision Outcome

Chosen option: **the newest C++ the toolchain takes, ownership explicit, leak gate always on.**

The project compiles as C++26 and prefers its facilities to C. `void*`, a
pointer-plus-count pair, a C array and a `#define` constant are escape hatches
and carry a reason. A feature the toolchain has not shipped — `std::expected`
under libstdc++ 13 with clang 18 is the live case — is gated by its feature-test
macro, never assumed.

A raw pointer is **non-owning**, or it is `nullptr`. Ownership is
`std::unique_ptr`; `std::shared_ptr` appears only where a lifetime is genuinely
shared. Non-owning sequences are `std::span` and `std::string_view`, and the
view never outlives its range. A copy is avoided by moving, and a duplicated
buffer counts as a defect.

Every development run is leak-checked, in two passes that cannot be merged.
The `asan` target builds and runs under AddressSanitizer; the `leak` target runs the leak
pass — LeakSanitizer on Linux, `leaks --atExit` against a `dev` build on macOS,
because `leaks` cannot read an ASan heap. `dsymutil` symbolizes the binary so a
report names frames, and no debug build ships without line info. A leak report is
a finding, not background noise.

`panic` logs and calls `std::abort()`; `std::unreachable()` marks UB, which is
weaker than a guaranteed abort, so it is not the panic path.

### Consequences

- Good: a signature states its bound and its element type.
- Good: ownership is visible in the type and enforced by the compiler.
- Good: a leak surfaces in the run that caused it.
- Bad: ASan runs are several times slower, so the suite runs under two presets.
- Bad: `std::shared_ptr` decisions need a review, not a reflex.
- Bad: feature macros add a conditional to the build.

### Confirmation

`submitConnect` takes `std::span<const std::byte>`; `grep -rn 'void \*' src`
finds no port signature; the `asan` and `leak` targets both pass on the host, and the
`dev-asan` preset carries `abort_on_error=1` plus a `dsymutil` post-build step.
