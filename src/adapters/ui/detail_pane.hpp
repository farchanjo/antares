#pragma once

#include <QStringList>
#include <QWidget>

class QFormLayout;
class QLabel;
class QToolButton;

namespace antares::adapters::ui {

/// The right pane: a header with the object and its actions, then sections.
///
/// The reference puts a sectioned panel here — a header with edit, delete and
/// close, and collapsible `Metrics`, `Properties` and `Deploy Revisions` below.
/// A section is a disclosure: clicking its title folds the body away.
class DetailPane final : public QWidget {
    Q_OBJECT

public:
    explicit DetailPane(QWidget *parent = nullptr);

    /// Show an object. `kind` and `name` fill the header; `properties` are
    /// already-formatted `label: value` lines in display order.
    void show(const QString &kind, const QString &name, const QStringList &properties);

    /// Say that nothing is selected.
    void clear();

signals:
    /// The close action, so the window can drop the pane.
    void closed();
    /// The edit action, for the row the pane holds.
    void editRequested();
    /// The delete action, for the row the pane holds.
    void deleteRequested();

private:
    QLabel *heading_ = nullptr;
    QLabel *empty_ = nullptr;
    QWidget *body_ = nullptr;
    QFormLayout *fields_ = nullptr;
    QToolButton *close_ = nullptr;
    QToolButton *edit_ = nullptr;
    QToolButton *remove_ = nullptr;

    void dropFields();
    /// Build one collapsible section and return the widget its body lives in.
    QWidget *addSection(const QString &title, bool expanded);
};

} // namespace antares::adapters::ui
