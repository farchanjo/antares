#pragma once

#include <QColor>
#include <QString>
#include <QToolButton>

namespace antares::adapters::ui {

/// A cluster avatar: a filled circle with the cluster's initials.
///
/// The reference paints one per cluster down the left rail. The colour is
/// derived from the name, so the same cluster keeps its colour across runs
/// without anything being stored.
class Avatar final : public QToolButton {
    Q_OBJECT

public:
    explicit Avatar(QString name, QWidget *parent = nullptr);

    [[nodiscard]] const QString &clusterName() const noexcept { return name_; }

    /// The two letters shown. A short name keeps what it has; a long one is
    /// reduced to its first two words' initials.
    [[nodiscard]] static QString initialsFor(const QString &name);

    /// Stable colour for a name, so a cluster is recognisable before it is read.
    [[nodiscard]] static QColor colorFor(const QString &name);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString name_;
    QString initials_;
    QColor color_;
};

} // namespace antares::adapters::ui
