#pragma once

#include <string_view>

namespace antares::ports::log {

/// P-10 levels. `panic` logs and aborts; the rest only record.
enum class Level : unsigned char { trace, debug, info, warn, error, panic };

/// A log call names its level and the category of its source directory.
class Logger {
public:
    Logger() = default;
    Logger(const Logger &) = default;
    Logger &operator=(const Logger &) = default;
    Logger(Logger &&) = default;
    Logger &operator=(Logger &&) = default;
    virtual ~Logger() = default;

    virtual void write(Level level, std::string_view category, std::string_view message) = 0;

    [[nodiscard]] virtual Level threshold() const noexcept = 0;
    virtual void setThreshold(Level level) noexcept = 0;
};

} // namespace antares::ports::log
