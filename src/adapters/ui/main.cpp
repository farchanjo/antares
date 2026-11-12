#include <cstdio>
#include <string_view>

#include <QApplication>

#include "adapters/ui/application.hpp"

/// The process entry point. It owns nothing but the stack frame: `Application`
/// owns the order, and `main` only translates its outcome to an exit code
/// (ADR 0027).
int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    QApplication::setApplicationName(QStringLiteral("antares"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    antares::adapters::ui::Application application{app};
    if (auto built = application.build(); !built) {
        // One line, on stderr, with the family: a bare exit code would make the
        // operator guess which of four steps failed (FR-002).
        std::fprintf(stderr, "antares: startup failed: %.*s\n",
                     static_cast<int>(antares::adapters::ui::describe(built.error()).size()),
                     antares::adapters::ui::describe(built.error()).data());
        return 1;
    }

    const int code = QApplication::exec();
    // The loop is not running any more, so the drain pumps it itself.
    application.shutdown();
    return code;
}
