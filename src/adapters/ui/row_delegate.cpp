#include "adapters/ui/row_delegate.hpp"

#include <QApplication>
#include <QPainter>
#include <QStyleOptionButton>

#include "adapters/ui/family.hpp"
#include "adapters/ui/qt_list_model.hpp"

namespace antares::adapters::ui {

namespace {

constexpr int PILL_RADIUS = 3;
constexpr int PILL_MARGIN = 4;
constexpr int METER_HEIGHT = 5;
constexpr int METER_INSET = 8;
constexpr int CHECK_INSET = 22;

/// The colour a status word carries. Green runs, amber waits, red fails.
QColor statusColour(const QString &word)
{
    if (word.compare(QStringLiteral("Running"), Qt::CaseInsensitive) == 0 ||
        word.compare(QStringLiteral("Active"), Qt::CaseInsensitive) == 0 ||
        word.compare(QStringLiteral("Available"), Qt::CaseInsensitive) == 0) {
        return QColor{0x2E, 0x7D, 0x32};
    }
    if (word.compare(QStringLiteral("Progressing"), Qt::CaseInsensitive) == 0) {
        return QColor{0x15, 0x65, 0xC0};
    }
    if (word.compare(QStringLiteral("SchedulingDisab"), Qt::CaseInsensitive) == 0) {
        return QColor{0xEF, 0x6C, 0x00};
    }
    return QColor{0x75, 0x75, 0x75};
}

} // namespace

RowDelegate::RowDelegate(const QtListModel &model, QObject *parent)
    : QStyledItemDelegate(parent)
    , model_(model)
{
}

void RowDelegate::paintCheck(QPainter *painter, const QStyleOptionViewItem &option)
{
    QStyleOptionButton box;
    box.rect = option.rect;
    box.state = QStyle::State_Enabled;
    QApplication::style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &box, painter);
}

void RowDelegate::paintPill(QPainter *painter, const QStyleOptionViewItem &option,
                            const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QColor colour = statusColour(text);
    const QFontMetrics metrics = option.fontMetrics;
    const int width = metrics.horizontalAdvance(text) + 2 * PILL_MARGIN;
    const int halfText = metrics.height() / 2;
    const QRect pill{option.rect.left() + 2, option.rect.center().y() - halfText,
                     qMin(width, option.rect.width() - 4), metrics.height()};
    painter->setPen(Qt::NoPen);
    QColor fill = colour;
    fill.setAlpha(48);
    painter->setBrush(fill);
    painter->drawRoundedRect(pill, PILL_RADIUS, PILL_RADIUS);
    painter->setPen(colour);
    painter->drawText(pill, Qt::AlignCenter, text);
    painter->restore();
}

void RowDelegate::paintMeter(QPainter *painter, const QStyleOptionViewItem &option,
                             const QString &text)
{
    // The value is a fraction the model formatted as `x%`; without it the meter
    // is a faint track so the column still reads as a column.
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    constexpr int HALF_HEIGHT = METER_HEIGHT / 2;
    const QRect track{option.rect.left() + METER_INSET,
                      option.rect.center().y() - HALF_HEIGHT,
                      qMax(option.rect.width() - 2 * METER_INSET, 10), METER_HEIGHT};
    painter->setPen(Qt::NoPen);
    QColor faint = option.palette.mid().color();
    faint.setAlpha(90);
    painter->setBrush(faint);
    painter->drawRoundedRect(track, HALF_HEIGHT, HALF_HEIGHT);

    // The model writes `x%`; an empty cell would make `chopped` read past the
    // start, so the suffix is dropped only when it is really there.
    bool ok = false;
    const double percent = text.endsWith(QLatin1Char('%')) ? text.chopped(1).toDouble(&ok) : 0;
    if (ok && percent > 0) {
        const QColor fill{0x15, 0x65, 0xC0};
        QRect filled = track;
        filled.setWidth(static_cast<int>(static_cast<double>(track.width()) * qMin(percent, 100.0) / 100.0));
        painter->setBrush(fill);
        painter->drawRoundedRect(filled, HALF_HEIGHT, HALF_HEIGHT);
    }
    painter->restore();
}

void RowDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                        const QModelIndex &index) const
{
    const std::span<const RowRole> columns = familyColumns(model_.family());
    if (index.column() < 0 || index.column() >= static_cast<int>(columns.size())) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    switch (columns[static_cast<std::size_t>(index.column())]) {
    case RowRole::name: {
        // The name column carries the selection box before the text.
        QStyleOptionViewItem shifted = option;
        shifted.rect.setLeft(option.rect.left() + CHECK_INSET);
        QStyleOptionButton box;
        box.rect = QRect{option.rect.left() + 4, option.rect.center().y() - 7, 14, 14};
        box.state = QStyle::State_Enabled;
        if (model_.isTicked(static_cast<std::uint32_t>(index.row()))) {
            box.state |= QStyle::State_On;
        }
        QApplication::style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &box, painter);
        QStyledItemDelegate::paint(painter, shifted, index);
        return;
    }
    case RowRole::cpu:
    case RowRole::memory:
    case RowRole::disk: {
        QStyleOptionViewItem base = option;
        base.text.clear();
        QStyledItemDelegate::paint(painter, base, index);
        paintMeter(painter, option, index.data(Qt::DisplayRole).toString());
        return;
    }
    case RowRole::actions: {
        QStyleOptionViewItem base = option;
        base.text.clear();
        QStyledItemDelegate::paint(painter, base, index);
        painter->save();
        painter->setPen(option.palette.mid().color());
        painter->drawText(option.rect, Qt::AlignCenter, QStringLiteral("\u22EE"));
        painter->restore();
        return;
    }
    case RowRole::status:
    case RowRole::conditions: {
        QStyleOptionViewItem base = option;
        base.text.clear();
        QStyledItemDelegate::paint(painter, base, index);
        paintPill(painter, option, index.data(Qt::DisplayRole).toString());
        return;
    }
    default:
        QStyledItemDelegate::paint(painter, option, index);
    }
}

} // namespace antares::adapters::ui
