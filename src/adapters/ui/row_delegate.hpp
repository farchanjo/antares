#pragma once

#include <QStyledItemDelegate>

namespace antares::adapters::ui {

class QtListModel;

/// Paints the cells that are not text.
///
/// The reference draws a selection checkbox in the first column, a status pill
/// and a coloured meter for CPU, memory and disk. Everything else stays text and
/// falls through to the base class.
class RowDelegate final : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit RowDelegate(const QtListModel &model, QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

private:
    const QtListModel &model_;

    static void paintCheck(QPainter *painter, const QStyleOptionViewItem &option);
    static void paintPill(QPainter *painter, const QStyleOptionViewItem &option,
                          const QString &text);
    static void paintMeter(QPainter *painter, const QStyleOptionViewItem &option,
                           const QString &text);
};

} // namespace antares::adapters::ui
