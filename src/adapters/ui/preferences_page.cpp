#include "adapters/ui/preferences_page.hpp"

#include <QCheckBox>

#include <cstdio>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace antares::adapters::ui {

namespace {

/// A switch with its label, the reference's toggle.
QCheckBox *toggle(bool on)
{
    auto *box = new QCheckBox{};
    box->setChecked(on);
    return box;
}

QComboBox *combo(std::initializer_list<const char *> items, const QString &current)
{
    auto *box = new QComboBox{};
    for (const char *item : items) {
        box->addItem(QString::fromUtf8(item));
    }
    box->setCurrentText(current);
    return box;
}

QSpinBox *number(int value, int maximum)
{
    auto *box = new QSpinBox{};
    box->setRange(0, maximum);
    box->setValue(value);
    return box;
}

} // namespace

PreferencesPage::PreferencesPage(QWidget *parent)
    : QWidget(parent)
    , rail_(new QListWidget{this})
    , pages_(new QStackedWidget{this})
{
    auto *layout = new QHBoxLayout{this};
    layout->setContentsMargins({});
    layout->setSpacing(0);

    rail_->setFixedWidth(160);
    rail_->setFrameShape(QFrame::NoFrame);
    layout->addWidget(rail_);

    layout->addWidget(pages_, 1);

    const struct {
        const char *label;
        void (*build)(QWidget *);
    } entries[] = {
        {"App", &PreferencesPage::buildApp},
        {"Proxy", nullptr},
        {"Kubernetes", nullptr},
        {"Editor", &PreferencesPage::buildEditor},
        {"Tables", &PreferencesPage::buildTables},
        {"Terminal", &PreferencesPage::buildTerminal},
        {"Integrations", nullptr},
        {"Telemetry", &PreferencesPage::buildTelemetry},
        {"Lens Teamwork", nullptr},
        {"External Tools", nullptr},
    };
    for (const auto &entry : entries) {
        QWidget *page = buildPage(QString::fromUtf8(entry.label));
        if (entry.build != nullptr) {
            entry.build(page);
        }
        pages_->addWidget(page);
        rail_->addItem(QString::fromUtf8(entry.label));
    }

    QObject::connect(rail_, &QListWidget::currentRowChanged, pages_,
                     &QStackedWidget::setCurrentIndex);
    rail_->setCurrentRow(0);
}

QWidget *PreferencesPage::buildPage(const QString &title)
{
    // No scroll area: a page is short, and a wrapped label inside one measures
    // zero high before its width settles, which swallowed every description.
    auto *page = new QWidget{pages_};
    auto *layout = new QVBoxLayout{page};
    layout->setContentsMargins(24, 18, 24, 18);
    layout->setSpacing(18);

    auto *heading = new QLabel{title, page};
    QFont font = heading->font();
    font.setPointSizeF(font.pointSizeF() * 1.4);
    heading->setFont(font);
    layout->addWidget(heading);
    return page;
}

void PreferencesPage::addSection(QWidget *page, const QString &title, const QString &description,
                                 QWidget *control)
{
    QWidget *host = page;
    auto *layout = dynamic_cast<QVBoxLayout *>(host->layout());
    if (layout == nullptr) {
        return;
    }
    auto *section = new QWidget{host};
    auto *sectionLayout = new QVBoxLayout{section};
    sectionLayout->setContentsMargins({});
    sectionLayout->setSpacing(2);

    auto *row = new QHBoxLayout{};
    auto *label = new QLabel{title.toUpper(), section};
    QFont small = label->font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    small.setBold(true);
    label->setFont(small);
    row->addWidget(label, 1);
    if (control != nullptr) {
        control->setParent(section);
        row->addWidget(control, 0, Qt::AlignRight);
    }
    sectionLayout->addLayout(row);

    int hintVisible = 0;
    if (!description.isEmpty()) {
        // The description is a plain label with the default palette: a styled
        // one inherited nothing and painted invisible.
        auto *hint = new QLabel{description, section};
        hint->setWordWrap(true);
        sectionLayout->addWidget(hint);
        hintVisible = 1;
    }

    section->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    layout->addWidget(section);
    std::fprintf(stderr, "  [sec] %s sec=%zu filho=%d hint=%d\n", qPrintable(title),
                 static_cast<std::size_t>(sectionLayout->count()), section->isVisible(),
                 hintVisible);
}

void PreferencesPage::buildApp(QWidget *page)
{
    addSection(page, tr("Theme"), tr("Sync with computer"),
               combo({"Sync with computer", "Dark", "Light"}, tr("Sync with computer")));
    addSection(page, tr("Lens CLI"),
               tr("Install 'lens' command to /usr/local/bin on macOS/Linux. On Windows 'lens' "
                  "command is automatically available."),
               toggle(false));
    addSection(page, tr("Colorized clusters"),
               tr("Differentiate Kubernetes clusters by color. A specific color can be set in the "
                  "cluster settings."),
               toggle(true));
    addSection(page, tr("Start-up"), tr("Automatically start Lens on login"), toggle(false));
    addSection(page, tr("Update channel"), QString{}, combo({"Stable", "Beta"}, tr("Stable")));
    addSection(page, tr("Locale timezone"), QString{},
               combo({"America/Sao_Paulo", "UTC", "System"}, tr("America/Sao_Paulo")));
}

void PreferencesPage::buildEditor(QWidget *page)
{
    addSection(page, tr("Minimap"), tr("Show minimap"),
               combo({"right", "left"}, tr("right")));
    addSection(page, tr("Line numbers"), QString{}, combo({"On", "Off"}, tr("On")));
    addSection(page, tr("Tab size"), QString{}, number(2, 16));
    addSection(page, tr("Font size"), QString{}, number(12, 48));
    auto *family = new QLineEdit{QStringLiteral("RobotoMono")};
    addSection(page, tr("Font family"), QString{}, family);
}

void PreferencesPage::buildTerminal(QWidget *page)
{
    auto *shell = new QLineEdit{QStringLiteral("/bin/zsh")};
    addSection(page, tr("Terminal shell path"), QString{}, shell);
    addSection(page, tr("Terminal copy & paste"), tr("Copy on select and paste on right-click"),
               toggle(true));
    addSection(page, tr("Terminal theme"), QString{}, combo({"Match Lens Theme"}, tr("Match Lens Theme")));
    addSection(page, tr("Font size"), QString{}, number(12, 48));
    addSection(page, tr("Font family"), QString{}, combo({"RobotoMono"}, tr("RobotoMono")));
}

void PreferencesPage::buildTables(QWidget *page)
{
    addSection(page, tr("Row line height"), QString{}, number(20, 64));
}

void PreferencesPage::buildTelemetry(QWidget *page)
{
    addSection(page, tr("In-app surveys"),
               tr("This will allow you to participate in surveys to improve the Lens experience."),
               toggle(true));
    addSection(page, tr("Telemetry & usage tracking"),
               tr("Telemetry & usage data is collected to continuously improve the Lens "
                  "experience."),
               toggle(true));
}

} // namespace antares::adapters::ui
