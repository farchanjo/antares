#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <QSocketNotifier>

#include "adapters/http/client.hpp"
#include "adapters/k8s/http_kube_client.hpp"
#include "adapters/log/console_logger.hpp"
#include "adapters/ui/main_window.hpp"
#include "adapters/ui/resource_model.hpp"
#include "application/io/engine_pool.hpp"
#include "application/k8s/object_store.hpp"

class QApplication;

namespace antares::adapters::ui {

/// Why startup stopped (FR-002). The family is what the operator reads; the
/// detail is logged where it happens, because a message that repeats the log is
/// a second copy to keep true.
enum class StartupError : std::uint8_t {
    engine,
    kubeconfig,
    client,
    window,
};

[[nodiscard]] std::string_view describe(StartupError error) noexcept;

/// The one place that owns the construction and destruction order (ADR 0027).
///
/// `build()` creates in dependency order and unwinds what it created when a step
/// fails. `shutdown()` runs the reverse after the event loop returned, with the
/// engine last: every other component posts to it, so freeing it first turns a
/// late completion into a crash.
///
/// The loop is hosted, not run (ADR 0020): a `QSocketNotifier` over
/// `waitHandle()` drives `runOnce()`, and the same notifier mechanism carries
/// `SIGTERM`/`SIGINT` through a self-pipe to `quit()`.
class Application final {
public:
    explicit Application(QApplication &app);
    ~Application();

    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;
    Application(Application &&) = delete;
    Application &operator=(Application &&) = delete;

    /// Build in dependency order, then show. A failure undoes the steps already
    /// done, in reverse, before it returns.
    [[nodiscard]] std::expected<void, StartupError> build();

    /// Unwind after `exec()` returned. Safe to call twice, and safe after a
    /// failed `build()`.
    void shutdown();

    [[nodiscard]] MainWindow *window() const noexcept { return window_.get(); }
    [[nodiscard]] QSocketNotifier *wakeupNotifier() const noexcept { return wakeupNotifier_; }
    [[nodiscard]] QSocketNotifier *signalNotifier() const noexcept { return signalNotifier_; }

    /// The destruction order, oldest first. A case asserts it rather than
    /// trusting a comment, because this is exactly where the bug hides.
    [[nodiscard]] std::span<const std::string_view> trace() const noexcept { return trace_; }

private:
    /// Pump the loop until the client has nothing in flight or the drain bound
    /// expires (FR-004). A hosted loop processes nothing after `exec()`, so a
    /// passive wait would deadlock.
    void drain();

    void installSignalHandler();
    void uninstallSignalHandler();
    void closeSignalPipe();

    /// Reverse order, engine last. Called from `shutdown()` and from the
    /// failure path of `build()`.
    void destroy();

    void trace(std::string_view step) { trace_.push_back(step); }

    QApplication &app_;

    /// The pool is the owner because the HTTP client is written against it; it
    /// holds exactly one engine, which is the one this process hosts.
    application::io::EnginePool pool_{1};
    ports::io::IoEngine *engine_ = nullptr;

    /// Declared before the client: `ClientConfig` keeps a non-owning pointer to
    /// it, so the logger must outlive every submission.
    log::ConsoleLogger logger_{ports::log::Level::info};

    std::shared_ptr<ports::http::Client> client_;
    std::shared_ptr<k8s::HttpKubeClient> kube_;
    std::unique_ptr<application::k8s::ObjectStore> store_;
    /// The scope the model shows. Built with the store, before the window.
    domain::k8s::Scope scope_;
    std::unique_ptr<ResourceModel> model_;
    std::unique_ptr<MainWindow> window_;

    QSocketNotifier *wakeupNotifier_ = nullptr;
    QSocketNotifier *signalNotifier_ = nullptr;
    int signalPipe_[2] = {-1, -1};

    std::vector<std::string_view> trace_;
    bool handlerInstalled_ = false;
    bool built_ = false;
};

} // namespace antares::adapters::ui
