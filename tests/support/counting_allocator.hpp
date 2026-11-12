#pragma once

#include <cstddef>

namespace antares::test {

/// Bytes currently held through `operator new`.
///
/// The store's per-object cost is counted here rather than read from RSS, so
/// the case is deterministic and a regression names a number (P-03, FR-003).
inline std::size_t liveBytes = 0;

/// Live allocations through `operator new`, the count feature 008 pins: a
/// scanner that decodes only the kept fields allocates a handful of strings,
/// where a built document allocates one per member (FR-002).
inline std::size_t liveAllocations = 0;

namespace detail {
inline bool active = false;
} // namespace detail

/// Turn the counter on for a scope and off again.
class CountingScope {
public:
    CountingScope() { detail::active = true; }
    ~CountingScope() { detail::active = false; }
    CountingScope(const CountingScope &) = delete;
    CountingScope &operator=(const CountingScope &) = delete;
};

[[nodiscard]] inline std::size_t live() noexcept
{
    return liveBytes;
}

[[nodiscard]] inline std::size_t allocations() noexcept
{
    return liveAllocations;
}

} // namespace antares::test

// Replacement functions cannot be inline, so the definitions live in
// `counting_allocator.cpp` and exactly one test translation unit links them.
void *operator new(std::size_t size);
void *operator new[](std::size_t size);
void operator delete(void *pointer) noexcept;
void operator delete[](void *pointer) noexcept;
void operator delete(void *pointer, std::size_t size) noexcept;
void operator delete[](void *pointer, std::size_t size) noexcept;
