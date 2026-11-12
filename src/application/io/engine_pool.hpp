#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

#include "ports/io/io_engine.hpp"
#include "ports/log/logger.hpp"

namespace antares::application::io {

/// A fixed set of engines, one loop thread each, owned here.
///
/// Work is bound to a loop for its lifetime, so nothing is shared between loops
/// and no lock guards the connection state. The pool only decides *which* loop;
/// it never touches an operation.
class EnginePool {
public:
    /// `loops` is clamped to at least one.
    explicit EnginePool(std::size_t loops);
    ~EnginePool();

    EnginePool(const EnginePool &) = delete;
    EnginePool &operator=(const EnginePool &) = delete;

    /// Start every loop thread. Idempotent.
    void start();
    void stop();

    /// The loop the next unit of work should use. Round-robin, never the same
    /// one twice in a row while another is free.
    [[nodiscard]] ports::io::IoEngine &next() noexcept;

    /// Hand the logger to every engine. Null silences them.
    void setLogger(ports::log::Logger *logger) noexcept;

    /// True when the caller is any loop thread of this pool (FR-008).
    [[nodiscard]] bool isLoopThread() const noexcept;

    /// True while the loops run. A client checks it before waiting on a drain:
    /// a stopped pool never completes again, so the wait would only time out.
    [[nodiscard]] bool isRunning() const noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return engines_.size(); }
    [[nodiscard]] const std::vector<std::unique_ptr<ports::io::IoEngine>> &engines() const noexcept
    {
        return engines_;
    }

private:
    std::vector<std::unique_ptr<ports::io::IoEngine>> engines_;
    std::vector<std::thread> threads_;
    std::atomic<std::size_t> cursor_{0};
    std::atomic<bool> running_{false};
};

} // namespace antares::application::io
