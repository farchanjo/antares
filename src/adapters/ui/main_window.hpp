#pragma once

#include <QMainWindow>
#include <QString>

class QLabel;
class QListView;

namespace antares::adapters::ui {

class QtListModel;

/// The only widget this feature owns (scope of 010).
///
/// A list, a status line and a title. Feature 009 replaces the list's
/// configuration with the batched layout it specifies; the window itself stays
/// the same, which is why the message is passed in rather than built here.
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    /// `model` is not owned: the application keeps it and destroys the window
    /// first (FR-004). `context` names the current context, and `message` is the
    /// reason the list is empty, if any.
    MainWindow(QtListModel &model, const QString &context, const QString &message,
               QWidget *parent = nullptr);

    /// The status line, so a case can assert the message and not just the widget.
    [[nodiscard]] QLabel *status() const noexcept;

private:
    QLabel *status_ = nullptr;
};

} // namespace antares::adapters::ui
