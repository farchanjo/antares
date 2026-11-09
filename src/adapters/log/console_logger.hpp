#pragma once

#include <cstdio>
#include <string_view>

#include "ports/log/logger.hpp"

namespace antares::adapters::log {

/// Writes one line per call to a stream. The default sink for development.
class ConsoleLogger final : public ports::log::Logger {
public:
    explicit ConsoleLogger(ports::log::Level threshold = ports::log::Level::info,
                           std::FILE *stream = stderr)
        : threshold_(threshold)
        , stream_(stream)
    {
    }

    void write(ports::log::Level level, std::string_view category,
               std::string_view message) override
    {
        if (level < threshold_ || stream_ == nullptr) {
            return;
        }
        (void)std::fprintf(stream_, "%-5s [%.*s] %.*s\n", name(level),
                           static_cast<int>(category.size()), category.data(),
                           static_cast<int>(message.size()), message.data());
        (void)std::fflush(stream_);
    }

    [[nodiscard]] ports::log::Level threshold() const noexcept override { return threshold_; }
    void setThreshold(ports::log::Level level) noexcept override { threshold_ = level; }

    [[nodiscard]] static constexpr std::string_view name(ports::log::Level level) noexcept
    {
        switch (level) {
        case ports::log::Level::trace:
            return "trace";
        case ports::log::Level::debug:
            return "debug";
        case ports::log::Level::info:
            return "info";
        case ports::log::Level::warn:
            return "warn";
        case ports::log::Level::error:
            return "error";
        case ports::log::Level::panic:
            return "panic";
        }
        return "info";
    }

private:
    ports::log::Level threshold_;
    std::FILE *stream_;
};

} // namespace antares::adapters::log
