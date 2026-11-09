#pragma once

#include <cstddef>
#include <cstdint>

namespace antares::domain {

/// A byte count. Distinct from a duration and from a stream count.
class Bytes {
public:
    constexpr Bytes() = default;
    constexpr explicit constexpr Bytes(std::size_t value) noexcept
        : value_(value)
    {
    }

    [[nodiscard]] constexpr std::size_t count() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isZero() const noexcept { return value_ == 0; }

    [[nodiscard]] constexpr Bytes &operator+=(Bytes other) noexcept
    {
        value_ += other.value_;
        return *this;
    }

    friend constexpr Bytes operator+(Bytes left, Bytes right) noexcept
    {
        return Bytes{left.value_ + right.value_};
    }

    friend constexpr auto operator<=>(const Bytes &left, const Bytes &right) noexcept = default;

private:
    std::size_t value_ = 0;
};

namespace literals {

[[nodiscard]] constexpr Bytes operator""_B(unsigned long long value) noexcept
{
    return Bytes{static_cast<std::size_t>(value)};
}

[[nodiscard]] constexpr Bytes operator""_KiB(unsigned long long value) noexcept
{
    return Bytes{static_cast<std::size_t>(value) * 1024};
}

[[nodiscard]] constexpr Bytes operator""_MiB(unsigned long long value) noexcept
{
    return Bytes{static_cast<std::size_t>(value) * 1024 * 1024};
}

} // namespace literals

} // namespace antares::domain
