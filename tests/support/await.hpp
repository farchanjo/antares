#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace antares::test {

/// Block until a completion signals, without polling. The wait is on this
/// object's own mutex, never on engine state.
class Await {
public:
    void signal()
    {
        {
            const std::lock_guard<std::mutex> guard{mutex_};
            signalled_ = true;
        }
        settled_.notify_all();
    }

    [[nodiscard]] bool wait(std::chrono::milliseconds timeout = std::chrono::seconds{5})
    {
        std::unique_lock<std::mutex> lock{mutex_};
        return settled_.wait_for(lock, timeout, [this] { return signalled_; });
    }

    [[nodiscard]] bool signalled() const
    {
        const std::lock_guard<std::mutex> guard{mutex_};
        return signalled_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable settled_;
    bool signalled_ = false;
};

} // namespace antares::test
