#pragma once

#include <cstddef>
#include <functional>

#include "adapters/ui/model_wakeup.hpp"

namespace antares::test::ui {

/// The queued invocation, without an event loop: the wake runs the callback in
/// place, which is what the GUI thread would do a moment later.
class RecordingWakeup : public adapters::ui::ModelWakeup {
public:
    void wake() override
    {
        ++wakes;
        if (onWake) {
            onWake();
        }
    }

    std::size_t wakes = 0;
    std::function<void()> onWake;
};

} // namespace antares::test::ui
