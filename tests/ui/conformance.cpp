#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QTimer>
#include <unistd.h>

#include "adapters/ui/application.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view what)
{
    if (condition) {
        return;
    }
    std::printf("  FAIL %.*s\n", static_cast<int>(what.size()), what.data());
    ++failures;
}

/// A kubeconfig good enough to resolve a binding without touching a cluster.
/// The server is never dialled at startup, so the name need not resolve.
constexpr std::string_view FIXTURE = R"(apiVersion: v1
kind: Config
current-context: dev
clusters:
- name: alpha
  cluster:
    server: https://alpha:6443
    insecure-skip-tls-verify: true
contexts:
- name: dev
  context:
    cluster: alpha
    user: builder
    namespace: work
users:
- name: builder
  user:
    token: s3cr3t
)";

/// Write the fixture to a temporary file and point `KUBECONFIG` at it.
[[nodiscard]] std::string installFixture()
{
    std::array<char, 64> path{};
    std::snprintf(path.data(), path.size(), "/tmp/antares-ui-XXXXXX");
    const int fd = ::mkstemp(path.data());
    if (fd < 0) {
        return {};
    }
    (void)::write(fd, FIXTURE.data(), FIXTURE.size());
    (void)::close(fd);
    (void)::setenv("KUBECONFIG", path.data(), 1);
    return {path.data()};
}

void removeFixture(const std::string &path)
{
    if (!path.empty()) {
        (void)::unlink(path.c_str());
    }
}

/// T008 — the window and both notifiers exist, and shutdown is the reverse of
/// construction with the engine last (FR-001, FR-004).
void caseBuildAndShutdown(QApplication &app)
{
    const std::string path = installFixture();
    check(!path.empty(), "fixture written");

    antares::adapters::ui::Application application{app};
    const auto built = application.build();
    check(built.has_value(), "build succeeds against the fixture");
    if (built) {
        check(application.window() != nullptr, "window exists");
        check(application.wakeupNotifier() != nullptr, "engine wakeup notifier installed");
        check(application.signalNotifier() != nullptr, "signal notifier installed");
        check(application.trace().empty(), "nothing destroyed before shutdown");
    }

    application.shutdown();

    const std::array<std::string_view, 6> expected{
        "window", "model", "store", "kube", "client", "engine"};
    check(application.trace().size() == expected.size(), "every step destroyed once");
    for (std::size_t index = 0; index < expected.size() && index < application.trace().size();
         ++index) {
        check(application.trace()[index] == expected[index], "destruction runs in reverse");
    }
    check(application.window() == nullptr, "window released");
    removeFixture(path);
}

/// T007 — two instances start without writing the kubeconfig (FR-007). The
/// loader opens it read-only, so the bytes must be identical afterwards.
void caseTwoInstances(QApplication &app)
{
    const std::string path = installFixture();
    check(!path.empty(), "fixture written");

    antares::adapters::ui::Application first{app};
    antares::adapters::ui::Application second{app};
    check(first.build().has_value(), "first instance builds");
    check(second.build().has_value(), "second instance builds");

    std::string after;
    if (std::FILE *file = std::fopen(path.c_str(), "rb")) {
        std::array<char, 1024> buffer{};
        const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), file);
        after.assign(buffer.data(), read);
        (void)std::fclose(file);
    }
    check(after == FIXTURE, "kubeconfig is untouched");

    second.shutdown();
    first.shutdown();
    removeFixture(path);
}

/// T009 — a raised `SIGTERM` reaches the self-pipe and `exec()` returns inside
/// the bound (FR-005).
void caseSigtermReturns(QApplication &app)
{
    const std::string path = installFixture();
    antares::adapters::ui::Application application{app};
    const auto built = application.build();
    check(built.has_value(), "build succeeds against the fixture");
    if (!built) {
        removeFixture(path);
        return;
    }

    // Raise from the loop, not from `main`: before `exec()` the handler would
    // write the pipe and the notifier would not run until the loop starts.
    QTimer::singleShot(0, [] { (void)::raise(SIGTERM); });

    QElapsedTimer clock;
    clock.start();
    const int code = app.exec();
    const qint64 elapsed = clock.elapsed();

    check(code == 0, "clean exit code");
    check(elapsed < 2000, "exec() returns inside the bound");

    application.shutdown();
    removeFixture(path);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::printf("usage: antares_ui_conformance <case>\n");
        return 2;
    }

    QApplication app{argc, argv};
    const std::string_view wanted{argv[1]};

    std::printf("%.*s:\n", static_cast<int>(wanted.size()), wanted.data());
    if (wanted == "build-and-shutdown") {
        caseBuildAndShutdown(app);
    } else if (wanted == "two-instances") {
        caseTwoInstances(app);
    } else if (wanted == "sigterm-returns") {
        caseSigtermReturns(app);
    } else {
        std::printf("unknown case '%.*s'\n", static_cast<int>(wanted.size()), wanted.data());
        return 2;
    }
    std::printf("  %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
