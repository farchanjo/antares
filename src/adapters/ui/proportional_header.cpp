#include "adapters/ui/proportional_header.hpp"

#include <QResizeEvent>

#include <cstdio>

namespace antares::adapters::ui {

namespace {

void grow(QList<int> &list, int section)
{
    while (list.size() <= section) {
        list.append(0);
    }
}

} // namespace

ProportionalHeader::ProportionalHeader(QWidget *parent)
    : QHeaderView(Qt::Horizontal, parent)
{
    setStretchLastSection(false);
    setSectionsMovable(false);
    setSectionResizeMode(QHeaderView::Fixed);
    setMinimumSectionSize(24);
}

void ProportionalHeader::setWeight(int section, int weight, int floor)
{
    grow(weights_, section);
    grow(floors_, section);
    weights_[section] = weight;
    floors_[section] = floor;
}

void ProportionalHeader::clearWeights()
{
    weights_.clear();
    floors_.clear();
}

void ProportionalHeader::resizeEvent(QResizeEvent *event)
{
    QHeaderView::resizeEvent(event);
    share();
}

void ProportionalHeader::share()
{
    const int sections = count();
    if (sections == 0 || weights_.isEmpty()) {
        return;
    }
    // The header is as wide as its sections want, not as wide as the pane, so
    // measuring it would hand the last section a negative share. The viewport is
    // the width that actually exists.
    const QWidget *port = parentWidget() != nullptr ? parentWidget() : this;
    const int room = port->width();
    if (room <= 0) {
        return;
    }

    // The floors are handed out first and the rest is split by weight, so a
    // narrow pane shrinks the wide columns instead of hiding the last ones.
    int floorTotal = 0;
    int weightTotal = 0;
    for (int at = 0; at < sections; ++at) {
        floorTotal += at < floors_.size() ? floors_[at] : 40;
        weightTotal += at < weights_.size() ? weights_[at] : 1;
    }
    if (weightTotal <= 0) {
        return;
    }
    const int spare = room > floorTotal ? room - floorTotal : 0;
    std::fprintf(stderr, "  [share] room=%d pisos=%d secoes=%d\n", room, floorTotal, sections);

    int used = 0;
    for (int at = 0; at < sections; ++at) {
        const int floor = at < floors_.size() ? floors_[at] : 40;
        const int weight = at < weights_.size() ? weights_[at] : 1;
        const int want = floor + (spare * weight) / weightTotal;
        // The last section takes the rounding remainder, so the row fills the
        // pane exactly and no dead band shows on the right.
        const int sized = (at == sections - 1) ? room - used : want;
        resizeSection(at, sized > 0 ? sized : 1);
        used += sized;
    }
}

} // namespace antares::adapters::ui
