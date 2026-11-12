#include "adapters/ui/detail_pane.hpp"

#include <QFont>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace antares::adapters::ui {

DetailPane::DetailPane(QWidget *parent)
    : QWidget(parent)
    , body_(new QWidget{this})
    , fields_(new QFormLayout{body_})
{
    auto *layout = new QVBoxLayout{this};
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    heading_ = new QLabel{tr("Detail"), this};
    QFont headingFont = heading_->font();
    headingFont.setBold(true);
    heading_->setFont(headingFont);
    heading_->setWordWrap(true);
    layout->addWidget(heading_);

    empty_ = new QLabel{tr("Select a row."), this};
    empty_->setWordWrap(true);
    layout->addWidget(empty_);

    fields_->setContentsMargins({});
    fields_->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    fields_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addWidget(body_);
    body_->setVisible(false);

    layout->addStretch(1);
}

void DetailPane::dropFields()
{
    while (fields_->rowCount() > 0) {
        fields_->removeRow(0);
    }
}

void DetailPane::clear()
{
    dropFields();
    heading_->setText(tr("Detail"));
    empty_->setVisible(true);
    body_->setVisible(false);
}

void DetailPane::show(const QString &kind, const QString &name, const QStringList &properties)
{
    dropFields();
    heading_->setText(name.isEmpty() ? kind : QStringLiteral("%1 %2").arg(kind, name));
    empty_->setVisible(properties.isEmpty());
    body_->setVisible(!properties.isEmpty());
    for (const QString &line : properties) {
        const qsizetype colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0) {
            continue;
        }
        auto *label = new QLabel{line.left(colon), body_};
        label->setStyleSheet(QStringLiteral("color: palette(mid);"));
        auto *value = new QLabel{line.mid(colon + 1).trimmed(), body_};
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        fields_->addRow(label, value);
    }
}

} // namespace antares::adapters::ui
