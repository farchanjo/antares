#pragma once

#include <QMainWindow>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QToolBar;

namespace antares::adapters::ui {

class QtListModel;

/// The window: a scope bar, the list, a status line.
///
/// Every child sits in a layout (LR-001) and only the list expands (LR-002), so
/// the rows take the slack at any window size and the controls keep their
/// height. The minimum comes from the layout, not from a literal (LR-003).
///
/// Every visible string goes through `tr()` (I18N-001); the catalogue is
/// installed by the application before this window exists.
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

    /// The namespace selector and the filter box, likewise for the cases.
    [[nodiscard]] QComboBox *namespaceBox() const noexcept;
    [[nodiscard]] QLineEdit *filterBox() const noexcept;

private:
    void buildBar();

    QtListModel &model_;
    QToolBar *bar_ = nullptr;
    QComboBox *namespaceBox_ = nullptr;
    QLineEdit *filterBox_ = nullptr;
    QListView *view_ = nullptr;
    QLabel *status_ = nullptr;
};

} // namespace antares::adapters::ui
