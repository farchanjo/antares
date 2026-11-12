#pragma once

#include <QHeaderView>
#include <QList>

namespace antares::adapters::ui {

/// A header that shares the width out by weight.
///
/// A fixed width per column runs out as soon as the pane narrows: the last
/// columns fall past the edge and the trailing menu becomes unreachable. This
/// header gives every section a share of what is actually there, keeping a floor
/// so a column never collapses to a sliver.
class ProportionalHeader final : public QHeaderView {
    Q_OBJECT

public:
    explicit ProportionalHeader(QWidget *parent = nullptr);

    /// Give `section` the weight `weight` (relative) and at least `floor` pixels.
    void setWeight(int section, int weight, int floor);
    /// Forget every weight; the sections then keep their own size.
    void clearWeights();
    /// Re-share `room` pixels now, without waiting for a resize.
    void shareNow(int room);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QList<int> weights_;
    QList<int> floors_;

    void share(int room);
};

} // namespace antares::adapters::ui
