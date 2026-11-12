#pragma once

#include <QStringList>
#include <QWidget>

class QFormLayout;
class QLabel;
class QVBoxLayout;

namespace antares::adapters::ui {

/// The right pane: the header of the object, then its labelled properties.
///
/// The reference puts a sectioned panel here, with the kind and name in a header
/// and a definition list under it. The list is the part a reader needs first, so
/// it is the part this build keeps.
class DetailPane final : public QWidget {
    Q_OBJECT

public:
    explicit DetailPane(QWidget *parent = nullptr);

    /// Show an object. `kind` and `name` fill the header; `properties` are
    /// already-formatted `label`/`value` pairs in display order.
    void show(const QString &kind, const QString &name, const QStringList &properties);

    /// Say that nothing is selected.
    void clear();

private:
    QLabel *heading_ = nullptr;
    QLabel *empty_ = nullptr;
    QWidget *body_ = nullptr;
    QFormLayout *fields_ = nullptr;

    void dropFields();
};

} // namespace antares::adapters::ui
