#include "adapters/ui/detail_pane.hpp"

#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace antares::adapters::ui {

namespace {

QToolButton *action(QWidget *parent, QStyle::StandardPixmap icon, const QString &tip)
{
    auto *button = new QToolButton{parent};
    button->setIcon(parent->style()->standardIcon(icon));
    button->setToolTip(tip);
    button->setAutoRaise(true);
    return button;
}

} // namespace

DetailPane::DetailPane(QWidget *parent)
    : QWidget(parent)
    , body_(new QWidget{this})
    , fields_(new QFormLayout{body_})
{
    auto *layout = new QVBoxLayout{this};
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *header = new QWidget{this};
    auto *headerLayout = new QHBoxLayout{header};
    headerLayout->setContentsMargins({});
    headerLayout->setSpacing(4);

    heading_ = new QLabel{tr("Detail"), header};
    QFont headingFont = heading_->font();
    headingFont.setBold(true);
    heading_->setFont(headingFont);
    heading_->setWordWrap(true);
    headerLayout->addWidget(heading_, 1);

    // The reference puts the actions here, in this order.
    edit_ = action(header, QStyle::SP_FileDialogDetailedView, tr("Edit"));
    remove_ = action(header, QStyle::SP_TrashIcon, tr("Delete"));
    close_ = action(header, QStyle::SP_DialogCloseButton, tr("Close"));
    headerLayout->addWidget(edit_);
    headerLayout->addWidget(remove_);
    headerLayout->addWidget(close_);
    layout->addWidget(header);

    QObject::connect(edit_, &QToolButton::clicked, this, &DetailPane::editRequested);
    QObject::connect(remove_, &QToolButton::clicked, this, &DetailPane::deleteRequested);
    QObject::connect(close_, &QToolButton::clicked, this, &DetailPane::closed);

    empty_ = new QLabel{tr("Select a row."), this};
    empty_->setWordWrap(true);
    layout->addWidget(empty_);

    QWidget *properties = addSection(tr("Properties"), true);
    auto *propertiesLayout = new QVBoxLayout{properties};
    propertiesLayout->setContentsMargins({});
    propertiesLayout->addWidget(body_);
    fields_->setContentsMargins({});
    fields_->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    fields_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    // The two sections this build has no data for yet: their titles are real, so
    // the shape matches and the gap is visible.
    static_cast<void>(addSection(tr("Metrics"), false));
    static_cast<void>(addSection(tr("Deploy Revisions"), false));

    layout->addStretch(1);
    clear();
}

QWidget *DetailPane::addSection(const QString &title, bool expanded)
{
    auto *section = new QWidget{this};
    auto *sectionLayout = new QVBoxLayout{section};
    sectionLayout->setContentsMargins({});
    sectionLayout->setSpacing(2);

    auto *toggle = new QToolButton{section};
    toggle->setText(title);
    toggle->setCheckable(true);
    toggle->setChecked(expanded);
    toggle->setAutoRaise(true);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    sectionLayout->addWidget(toggle);

    auto *content = new QWidget{section};
    content->setVisible(expanded);
    sectionLayout->addWidget(content);

    QObject::connect(toggle, &QToolButton::toggled, content, [toggle, content](bool open) {
        content->setVisible(open);
        toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    dynamic_cast<QVBoxLayout *>(layout())->addWidget(section);
    return content;
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
    edit_->setEnabled(false);
    remove_->setEnabled(false);
}

void DetailPane::show(const QString &kind, const QString &name, const QStringList &properties)
{
    dropFields();
    heading_->setText(name.isEmpty() ? kind : QStringLiteral("%1 %2").arg(kind, name));
    empty_->setVisible(properties.isEmpty());
    body_->setVisible(!properties.isEmpty());
    edit_->setEnabled(!name.isEmpty());
    remove_->setEnabled(!name.isEmpty());
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
