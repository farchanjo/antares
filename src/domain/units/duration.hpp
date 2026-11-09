#pragma once

#include <chrono>
#include <cstdint>

namespace antares::domain {

/// A span of time, stored in nanoseconds. Strong on purpose: an integer
/// millisecond is indistinguishable from an integer byte count (P-11).
class Duration {
public:
    using Storage = std::chrono::nanoseconds;

    constexpr Duration() = default;
    constexpr explicit Duration(Storage value) noexcept
        : value_(value)
    {
    }

    template <typename Rep, typename Period>
    [[nodiscard]] static constexpr Duration of(std::chrono::duration<Rep, Period> value) noexcept
    {
        return Duration{std::chrono::duration_cast<Storage>(value)};
    }

    [[nodiscard]] static constexpr Duration milliseconds(std::int64_t value) noexcept
    {
        return Duration{std::chrono::milliseconds{value}};
    }

    [[nodiscard]] static constexpr Duration seconds(std::int64_t value) noexcept
    {
        return Duration{std::chrono::seconds{value}};
    }

    [[nodiscard]] constexpr Duration &operator+=(Duration other) noexcept
    {
        value_ += other.value_;
        return *this;
    }

    [[nodiscard]] constexpr Duration &operator-=(Duration other) noexcept
    {
        value_ -= other.value_;
        return *this;
    }

    [[nodiscard]] constexpr std::int64_t nanoseconds() const noexcept { return value_.count(); }
    [[nodiscard]] constexpr std::int64_t milliseconds() const noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(value_).count();
    }
    [[nodiscard]] constexpr bool isZero() const noexcept { return value_.count() == 0; }
    [[nodiscard]] constexpr Storage chrono() const noexcept { return value_; }

    friend constexpr Duration operator+(Duration left, Duration right) noexcept
    {
        return Duration{left.value_ + right.value_};
    }

    friend constexpr Duration operator-(Duration left, Duration right) noexcept
    {
        return Duration{left.value_ - right.value_};
    }

    friend constexpr auto operator<=>(const Duration &left, const Duration &right) noexcept = default;

private:
    Storage value_{};
};

namespace literals {

[[nodiscard]] constexpr Duration operator""_ms(unsigned long long value) noexcept
{
    return Duration::milliseconds(static_cast<std::int64_t>(value));
}

[[nodiscard]] constexpr Duration operator""_s(unsigned long long value) noexcept
{
    return Duration::seconds(static_cast<std::int64_t>(value));
}

[[nodiscard]] constexpr Duration operator""_min(unsigned long long value) noexcept
{
    return Duration::seconds(static_cast<std::int64_t>(value) * 60);
}

} // namespace literals

} // namespace antares::domain
