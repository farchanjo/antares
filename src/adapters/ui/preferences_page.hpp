#pragma once

#include <QWidget>

class QListWidget;
class QStackedWidget;

namespace antares::adapters::ui {

/// The settings surface: a rail of sub-pages and the selected page beside it.
///
/// The reference orders the rail `App`, `Proxy`, `Kubernetes`, `Editor`, `Tables`,
/// `Terminal`, `Integrations`, `Telemetry`, `Lens Teamwork`, `External Tools`.
/// The pages that need no backend are built here; the rest carry their controls
/// as structure so the shape is complete.
class PreferencesPage final : public QWidget {
    Q_OBJECT

public:
    explicit PreferencesPage(QWidget *parent = nullptr);

private:
    QListWidget *rail_ = nullptr;
    QStackedWidget *pages_ = nullptr;

    /// A settings page: a title, then sections of title, description and control.
    [[nodiscard]] QWidget *buildPage(const QString &title);
    /// A section: small-caps title, grey description, and the control.
    static void addSection(QWidget *page, const QString &title, const QString &description,
                           QWidget *control);
    static void buildApp(QWidget *page);
    static void buildEditor(QWidget *page);
    static void buildTerminal(QWidget *page);
    static void buildTables(QWidget *page);
    static void buildTelemetry(QWidget *page);
};

} // namespace antares::adapters::ui
