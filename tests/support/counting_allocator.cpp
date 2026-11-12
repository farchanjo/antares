#include "support/counting_allocator.hpp"

#include <cstdlib>
#include <new>

namespace {

/// Sixteen bytes carry the size the matching `operator delete` needs; sixteen
/// is also the maximum alignment, so a block stays aligned for every type the
/// suite uses.
constexpr std::size_t HEADER = 16;

[[nodiscard]] void *allocate(std::size_t size)
{
    auto *block = static_cast<std::size_t *>(std::malloc(size + HEADER));
    if (block == nullptr) {
        throw std::bad_alloc{};
    }
    block[0] = size;
    if (antares::test::detail::active) {
        antares::test::liveBytes += size;
    }
    return reinterpret_cast<char *>(block) + HEADER;
}

void release(void *pointer) noexcept
{
    if (pointer == nullptr) {
        return;
    }
    auto *block = reinterpret_cast<std::size_t *>(static_cast<char *>(pointer) - HEADER);
    if (antares::test::detail::active) {
        antares::test::liveBytes -= block[0];
    }
    std::free(block);
}

} // namespace

void *operator new(std::size_t size)
{
    return allocate(size);
}

void *operator new[](std::size_t size)
{
    return allocate(size);
}

void operator delete(void *pointer) noexcept
{
    release(pointer);
}

void operator delete[](void *pointer) noexcept
{
    release(pointer);
}

void operator delete(void *pointer, std::size_t /*size*/) noexcept
{
    release(pointer);
}

void operator delete[](void *pointer, std::size_t /*size*/) noexcept
{
    release(pointer);
}
