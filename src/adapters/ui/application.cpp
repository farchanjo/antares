#include "adapters/ui/application.hpp"

#include <cstdio>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <optional>

#include <string>
#include <unistd.h>
#include <utility>

#include <QApplication>
#include <QTreeView>
#include <QString>

#include "adapters/k8s/kubeconfig_loader.hpp"
#include "domain/units/instant.hpp"

namespace antares::adapters::ui {

namespace {

/// One turn's worth of posted work before the host loop regains control. The
/// same bound the blocking loop uses (FR-009 of 004).
constexpr domain::Duration TURN_BUDGET{4};

/// How long shutdown may spend settling in-flight requests (FR-026 of 002).
constexpr domain::Duration DRAIN_BOUND = std::chrono::seconds{5};

/// The write end of the self-pipe, for the signal handler.
///
/// The handler may not touch `this`, and it needs one integer; a process-wide
/// slot is enough because the application is a singleton by nature. Two
/// `Application` objects in one process would share the last-installed pipe,
/// which the single-threaded `main` never does.
[[nodiscard]] std::atomic<int> &signalWriteFd()
{
    // Function-local so the file has no non-const global; constant-initialized,
    // which keeps the access valid from a signal handler.
    static std::atomic<int> fd{-1};
    return fd;
}

/// Async-signal-safe: `write` only. A full pipe is fine — a byte is already
/// pending, and the notifier will run.
extern "C" void onTerminate(int /*signal*/)
{
    const int fd = signalWriteFd().load(std::memory_order_relaxed);
    const char byte = 1;
    (void)::write(fd, &byte, 1);
}

} // namespace

std::string_view describe(StartupError error) noexcept
{
    switch (error) {
    case StartupError::engine:
        return "engine";
    case StartupError::kubeconfig:
        return "kubeconfig";
    case StartupError::client:
        return "client";
    case StartupError::window:
        return "window";
    }
    return "unknown";
}

Application::Application(QApplication &app)
    : app_(app)
{
}

Application::~Application() { shutdown(); }

void Application::installSignalHandler()
{
    // The pipe is created before the handler can fire, and both ends are
    // non-blocking: a handler that blocked on a full pipe would stall whatever
    // interrupted it.
    if (::pipe(signalPipe_) != 0) {
        logger_.write(ports::log::Level::warn, "ui", "self-pipe unavailable; signals fall back to default");
        return;
    }
    for (const int fd : signalPipe_) {
        const int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    }
    signalWriteFd().store(signalPipe_[1], std::memory_order_relaxed);

    struct sigaction action {};
    action.sa_handler = &onTerminate;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    (void)::sigaction(SIGTERM, &action, nullptr);
    (void)::sigaction(SIGINT, &action, nullptr);

    // A closed peer must be an error, not a death: the client reports it
    // (FR-027 of 002).
    (void)::signal(SIGPIPE, SIG_IGN);
    handlerInstalled_ = true;
}

void Application::uninstallSignalHandler()
{
    if (handlerInstalled_) {
        (void)::signal(SIGTERM, SIG_DFL);
        (void)::signal(SIGINT, SIG_DFL);
        handlerInstalled_ = false;
    }
    signalWriteFd().store(-1, std::memory_order_relaxed);
}

void Application::closeSignalPipe()
{
    // Only after the notifier is gone: a closed descriptor with a live notifier
    // is a ready notification on an fd the process may already have reused.
    for (int &fd : signalPipe_) {
        if (fd >= 0) {
            (void)::close(fd);
            fd = -1;
        }
    }
}

std::expected<void, StartupError> Application::build()
{
    if (built_) {
        return {};
    }

    // FR-001: the engine first. It is not a `QObject`, so it is owned by the
    // pool rather than by the widget tree; everything below posts to it.
    if (pool_.engines().empty() || !pool_.engines().front()) {
        logger_.write(ports::log::Level::error, "ui", "no I/O engine could be built");
        destroy();
        return std::unexpected(StartupError::engine);
    }
    engine_ = pool_.engines().front().get();
    if (engine_ == nullptr) {
        logger_.write(ports::log::Level::error, "ui", "no I/O engine could be built");
        destroy();
        return std::unexpected(StartupError::engine);
    }
    engine_->setLogger(&logger_);
    engine_->attach();
    logger_.write(ports::log::Level::info, "ui", "engine " + std::string{engine_->name()});

    // FR-006, FR-007: read once, read-only. A missing file and an absent current
    // context are both normal — the window opens empty and names the file.
    const std::vector<std::string> paths = k8s::KubeconfigLoader::searchPaths();
    const std::string path = paths.empty() ? std::string{"<no kubeconfig path>"} : paths.front();
    QString message;

    std::optional<domain::k8s::Kubeconfig> config;
    if (auto loaded = k8s::KubeconfigLoader::load(paths); loaded) {
        config = std::move(*loaded);
    } else if (loaded.error() != domain::k8s::ConfigError::missingFile) {
        logger_.write(ports::log::Level::error, "ui",
                      path + ": " + std::string{domain::k8s::describe(loaded.error())});
        destroy();
        return std::unexpected(StartupError::kubeconfig);
    } else {
        message = QStringLiteral("No kubeconfig at %1").arg(QString::fromUtf8(path.c_str()));
    }

    std::shared_ptr<k8s::HttpKubeClient> kube;
    QString context;
    std::string initialNamespace;
    if (config) {
        k8s::KubeconfigLoader::hydrate(*config);
        auto binding = k8s::ClusterBinding::resolve(*config);
        if (!binding) {
            if (binding.error() != domain::k8s::ConfigError::noCurrentContext) {
                logger_.write(ports::log::Level::error, "ui",
                              path + ": " + std::string{domain::k8s::describe(binding.error())});
                destroy();
                return std::unexpected(StartupError::kubeconfig);
            }
            message = QStringLiteral("%1 names no current context").arg(QString::fromUtf8(path.c_str()));
        } else {
            context = QString::fromUtf8(binding->context.name.c_str());
            initialNamespace = binding->context.defaultNamespace;
            // P-10: the context is logged at `info`, never the credential.
            logger_.write(ports::log::Level::info, "ui",
                          "context " + binding->context.name + " on " + binding->base);

            domain::http::ClientConfig http = binding->http;
            http.logger = &logger_;
            auto client = std::make_shared<http::HttpClient>(pool_, std::move(http));
            client_ = client;
            kube = k8s::HttpKubeClient::create(std::move(*binding), std::move(client));
            kube_ = kube;
        }
    }

    if (!message.isEmpty()) {
        logger_.write(ports::log::Level::info, "ui", message.toUtf8().constData());
    }

    // FR-001 continues in dependency order: store, model, window. The window
    // takes both by reference, so it must come last.
    // The initial scope: the current context, its current namespace, and `pods`.
    // Nothing else names a resource yet, so without this the path is empty and
    // the first page has nowhere to go.
    // `toStdString` is an undefined symbol here: Qt is libstdc++ and the tree is
    // libc++, so the bytes are read through `constData` instead.
    scope_.cluster = domain::k8s::ClusterName::of(context.toUtf8().constData())
                         .value_or(domain::k8s::ClusterName{});
    scope_.type = domain::k8s::GroupVersionResource{"", "v1", "pods"};
    scope_.kind = "Pod";
    scope_.namespaceName = initialNamespace.empty()
                               ? domain::k8s::NamespaceName{}
                               : domain::k8s::NamespaceName::of(initialNamespace)
                                     .value_or(domain::k8s::NamespaceName{});

    store_ = std::make_unique<application::k8s::ObjectStore>();
    pages_ = std::make_unique<KubePageSource>(*kube_, *store_);
    model_ = std::make_unique<QtListModel>(*store_, scope_, *pages_, &logger_);
    // Nothing else asks: a view only calls `fetchMore` on a scroll, so without
    // this the window opens on an empty list.
    if (model_->canFetchMore({})) {
        model_->fetchMore({});
    }
    // The store publishes every batch it applies; the model is the only reader,
    // so the two are wired here and nowhere else.
    store_->setListener([this](const domain::k8s::ChangeSet &changes) { model_->observe(changes); });

    // The window owns no loader: the lists are read once here and handed over.
    QStringList contexts;
    QStringList namespaces;
    if (config) {
        for (const domain::k8s::Context &known : config->contexts) {
            contexts.append(QString::fromUtf8(known.name.c_str()));
        }
        namespaces.append(QString::fromUtf8(initialNamespace.c_str()));
    }
    std::fprintf(stderr, "  [build] janela\n");
    window_ = std::make_unique<MainWindow>(*model_, kube_.get(), contexts, context, namespaces,
                                           message);
    std::fprintf(stderr, "  [build] janela ok\n");
    if (!window_) {
        destroy();
        return std::unexpected(StartupError::window);
    }

    installSignalHandler();

    // FR-005: a signal reaches `quit()` exactly like a close does. The notifier
    // is a child of the window, so it dies with the window and before the engine.
    if (signalPipe_[0] >= 0) {
        signalNotifier_ = new QSocketNotifier{signalPipe_[0], QSocketNotifier::Read, window_.get()};
        QObject::connect(signalNotifier_, &QSocketNotifier::activated, window_.get(), [this] {
            char scratch[64];
            while (::read(signalPipe_[0], scratch, sizeof scratch) > 0) {
            }
            QApplication::quit();
        });
    }

    // FR-003: the engine is a source in the Qt loop. The notifier carries
    // readiness; the budget comes from the nearest timer, so a retry or an idle
    // deadline is not late.
    if (engine_->waitHandle() >= 0) {
        wakeupNotifier_ =
            new QSocketNotifier{engine_->waitHandle(), QSocketNotifier::Read, window_.get()};
        QObject::connect(wakeupNotifier_, &QSocketNotifier::activated, window_.get(), [this] {
            domain::Duration budget = TURN_BUDGET;
            if (const auto deadline = engine_->nextDeadline()) {
                const auto until = std::chrono::duration_cast<domain::Duration>(
                    *deadline - std::chrono::steady_clock::now());
                budget = std::max(domain::Duration{0}, until);
            }
            engine_->runOnce(budget);
        });
    }

    std::fprintf(stderr, "  [build] show\n");
    window_->show();
    std::fprintf(stderr, "  [build] mostrada tabela=%dx%d linhas=%d\n", window_->table()->width(),
                 window_->table()->height(), window_->table()->model() == nullptr ? -1 : 0);
    built_ = true;
    return {};
}

void Application::drain()
{
    if (engine_ == nullptr) {
        return;
    }
    const auto deadline = std::chrono::steady_clock::now() + DRAIN_BOUND;
    while (client_ && client_->pending() > 0 && std::chrono::steady_clock::now() < deadline) {
        engine_->runOnce(TURN_BUDGET);
    }
    if (client_ && client_->pending() > 0) {
        logger_.write(ports::log::Level::warn, "ui",
                      "drain expired with " + std::to_string(client_->pending()) + " in flight");
    }
}

void Application::shutdown()
{
    if (!built_) {
        return;
    }
    uninstallSignalHandler();
    drain();
    destroy();
}

void Application::destroy()
{
    // Reverse of construction, and the engine last: every other component posts
    // to it, so a completion after its destruction would be a use-after-free.
    if (window_) {
        trace("window");
        window_.reset();
    }
    wakeupNotifier_ = nullptr;
    signalNotifier_ = nullptr;
    closeSignalPipe();
    if (model_) {
        trace("model");
        model_.reset();
    }
    if (store_) {
        trace("store");
        store_.reset();
    }
    if (kube_) {
        trace("kube");
        kube_.reset();
    }
    if (client_) {
        trace("client");
        client_.reset();
    }
    if (engine_ != nullptr) {
        trace("engine");
        engine_->shutdown();
        engine_ = nullptr;
    }
    uninstallSignalHandler();
    built_ = false;
}

} // namespace antares::adapters::ui
