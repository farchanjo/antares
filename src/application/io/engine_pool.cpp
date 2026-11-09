#include "application/io/engine_pool.hpp"

#include <algorithm>

#include "application/io/engine_factory.hpp"

namespace antares::application::io {

EnginePool::EnginePool(std::size_t loops)
    : engines_(std::max<std::size_t>(loops, 1))
{
    for (auto &slot : engines_) {
        auto built = makeEngine();
        if (built) {
            slot = std::move(*built);
        }
    }
}

EnginePool::~EnginePool()
{
    stop();
}

void EnginePool::start()
{
    if (running_.exchange(true)) {
        return;
    }

    threads_.reserve(engines_.size());
    for (auto &engine : engines_) {
        if (!engine) {
            continue;
        }
        threads_.emplace_back([pointer = engine.get()] {
            if (pointer != nullptr) {
                pointer->run();
            }
        });
    }
}

void EnginePool::stop()
{
    if (!running_.exchange(false)) {
        return;
    }

    for (auto &engine : engines_) {
        if (engine) {
            engine->stop();
        }
    }
    for (auto &thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();
}

ports::io::IoEngine &EnginePool::next() noexcept
{
    const std::size_t index = cursor_.fetch_add(1, std::memory_order_relaxed) % engines_.size();
    return *engines_[index];
}

bool EnginePool::isRunning() const noexcept
{
    return running_.load(std::memory_order_acquire);
}

bool EnginePool::isLoopThread() const noexcept
{
    for (const auto &engine : engines_) {
        if (engine && engine->isEngineThread()) {
            return true;
        }
    }
    return false;
}

} // namespace antares::application::io
