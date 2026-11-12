#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "ports/log/logger.hpp"

namespace antares::test::ui {

/// A log sink that keeps the level, the category and the message, so a case can
/// assert the failure family reached `warn` without a log file.
class RecordingLogger : public antares::ports::log::Logger {
public:
    struct Line {
        antares::ports::log::Level level = antares::ports::log::Level::trace;
        std::string category;
        std::string message;
    };

    void write(antares::ports::log::Level level, std::string_view category,
               std::string_view message) override
    {
        lines.push_back(Line{level, std::string{category}, std::string{message}});
    }

    [[nodiscard]] antares::ports::log::Level threshold() const noexcept override
    {
        return antares::ports::log::Level::trace;
    }
    void setThreshold(antares::ports::log::Level level) noexcept override { threshold_ = level; }

    [[nodiscard]] bool sawWarn(std::string_view category) const
    {
        for (const Line &line : lines) {
            if (line.level == antares::ports::log::Level::warn && line.category == category) {
                return true;
            }
        }
        return false;
    }

    std::vector<Line> lines;

private:
    antares::ports::log::Level threshold_ = antares::ports::log::Level::trace;
};

} // namespace antares::test::ui
