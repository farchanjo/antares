#include "adapters/ui/avatar.hpp"

#include <QPainter>
#include <QPaintEvent>

#include <QStringList>

namespace antares::adapters::ui {

namespace {

constexpr int AVATAR_SIZE = 30;
constexpr int RING_WIDTH = 2;

} // namespace

Avatar::Avatar(QString name, QWidget *parent)
    : QToolButton(parent)
    , name_(std::move(name))
    , initials_(initialsFor(name_))
    , color_(colorFor(name_))
{
    setCheckable(true);
    setAutoRaise(true);
    setFixedSize(AVATAR_SIZE + 2 * RING_WIDTH, AVATAR_SIZE + 2 * RING_WIDTH);
    setToolTip(name_);
}

QString Avatar::initialsFor(const QString &name)
{
    QStringList words = name.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    if (words.size() < 2) {
        words = name.split(QLatin1Char('_'), Qt::SkipEmptyParts);
    }
    if (words.size() >= 2) {
        return (words.at(0).left(1) + words.at(1).left(1)).toUpper();
    }
    return name.left(2).toUpper();
}

QColor Avatar::colorFor(const QString &name)
{
    // A small fixed palette beats a hash: the colours stay distinguishable and
    // the same name always lands on the same one.
    static constexpr int PALETTE_SIZE = 6;
    // `toStdString` is an undefined symbol here (Qt is libstdc++, the tree is
    // libc++), so the sum is taken over the UTF-8 bytes instead.
    std::size_t hash = 0;
    for (const char byte : name.toUtf8()) {
        hash = hash * 31 + static_cast<std::size_t>(static_cast<unsigned char>(byte));
    }
    switch (hash % PALETTE_SIZE) {
    case 0:
        return QColor{0xC6, 0x28, 0x28};
    case 1:
        return QColor{0x15, 0x65, 0xC0};
    case 2:
        return QColor{0x2E, 0x7D, 0x32};
    case 3:
        return QColor{0x6A, 0x1B, 0x9A};
    case 4:
        return QColor{0xEF, 0x6C, 0x00};
    default:
        return QColor{0x45, 0x4F, 0x5A};
    }
}

void Avatar::paintEvent(QPaintEvent *event)
{
    static_cast<void>(event);
    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF disc = rect().adjusted(RING_WIDTH, RING_WIDTH, -RING_WIDTH, -RING_WIDTH);
    if (isChecked()) {
        // The selected cluster carries a ring, not a different fill, so the
        // initials stay legible.
        painter.setPen(QPen{color_.lighter(160), RING_WIDTH});
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(rect().adjusted(0, 0, -1, -1));
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(isEnabled() ? color_ : color_.darker(140));
    painter.drawEllipse(disc);

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(11);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(disc, Qt::AlignCenter, initials_);
}

} // namespace antares::adapters::ui
