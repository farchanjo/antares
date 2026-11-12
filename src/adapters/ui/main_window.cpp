#include "adapters/ui/main_window.hpp"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QStatusBar>
#include <QTreeView>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "adapters/ui/qt_list_model.hpp"
#include "adapters/ui/view_config.hpp"

namespace antares::adapters::ui {

namespace {

constexpr int RAIL_WIDTH = 48;
constexpr int NAVIGATOR_WIDTH = 240;
constexpr int DETAIL_WIDTH = 360;

/// A rail button: icon over nothing, square, checkable, one per family.
QToolButton *railButton(QWidget *parent, const QString &text, const QString &tip)
{
    auto *button = new QToolButton{parent};
    button->setText(text);
    button->setToolTip(tip);
    button->setCheckable(true);
    button->setFixedSize(RAIL_WIDTH - 12, RAIL_WIDTH - 18);
    button->setAutoRaise(true);
    return button;
}

} // namespace

MainWindow::MainWindow(QtListModel &model, const QStringList &contexts, const QString &context,
                       const QStringList &namespaces, const QString &message, QWidget *parent)
    : QMainWindow(parent)
    , model_(model)
{
    // The context is data, not a translatable string (I18N-001).
    setWindowTitle(context.isEmpty() ? tr("antares") : tr("antares — %1").arg(context));
    resize(1100, 680);

    auto *central = new QWidget{this};
    auto *layout = new QVBoxLayout{central};
    layout->setContentsMargins({});

    buildToolBar();
    contextBox_->addItems(contexts);
    if (!context.isEmpty()) {
        contextBox_->setCurrentText(context);
    }
    namespaceBox_->addItem(tr("All namespaces"), QString{});
    namespaceBox_->addItems(namespaces);
    layout->addWidget(bar_);

    // R-1 | R-2 | R-4 | R-5, in one splitter so each width is the operator's.
    splitter_ = new QSplitter{Qt::Horizontal, central};
    splitter_->addWidget(buildRail());
    splitter_->addWidget(buildNavigator());
    splitter_->addWidget(buildTable());
    splitter_->addWidget(buildDetail());
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 0);
    splitter_->setStretchFactor(2, 1);
    splitter_->setStretchFactor(3, 0);
    splitter_->setChildrenCollapsible(false);
    splitter_->setSizes({RAIL_WIDTH, NAVIGATOR_WIDTH, 420, DETAIL_WIDTH});
    layout->addWidget(splitter_, 1);

    buildStatus(message);
    setCentralWidget(central);
    view_->setFocus();
}

void MainWindow::buildToolBar()
{
    bar_ = new QToolBar{this};
    bar_->setMovable(false);
    bar_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto *scope = new QWidget{bar_};
    auto *scopeLayout = new QHBoxLayout{scope};
    scopeLayout->setContentsMargins({});

    auto *contextLabel = new QLabel{tr("&Context"), scope};
    contextBox_ = new QComboBox{scope};
    contextLabel->setBuddy(contextBox_);
    contextBox_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    scopeLayout->addWidget(contextLabel);
    scopeLayout->addWidget(contextBox_);

    auto *namespaceLabel = new QLabel{tr("&Namespace"), scope};
    namespaceBox_ = new QComboBox{scope};
    namespaceLabel->setBuddy(namespaceBox_);
    namespaceBox_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    scopeLayout->addWidget(namespaceLabel);
    scopeLayout->addWidget(namespaceBox_);

    auto *kindLabel = new QLabel{tr("&Kind"), scope};
    kindBox_ = new QComboBox{scope};
    kindLabel->setBuddy(kindBox_);
    kindBox_->addItems({tr("Pods"), tr("Deployments"), tr("Services"), tr("Nodes")});
    scopeLayout->addWidget(kindLabel);
    scopeLayout->addWidget(kindBox_);

    bar_->addWidget(scope);

    auto *spacer = new QWidget{bar_};
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar_->addWidget(spacer);

    region_ = new QLabel{tr("not connected"), bar_};
    bar_->addWidget(region_);
}

QWidget *MainWindow::buildRail()
{
    auto *rail = new QFrame{this};
    rail->setFrameShape(QFrame::NoFrame);
    rail->setFixedWidth(RAIL_WIDTH);
    auto *railLayout = new QVBoxLayout{rail};
    railLayout->setContentsMargins(6, 6, 6, 6);
    railLayout->setSpacing(4);

    // The rail is a shortcut into the tree, not a second tree (R-1).
    const struct {
        const char *label;
        const char *tip;
    } families[] = {
        {QT_TR_NOOP("W"), QT_TR_NOOP("Workloads")},
        {QT_TR_NOOP("C"), QT_TR_NOOP("Config")},
        {QT_TR_NOOP("N"), QT_TR_NOOP("Network")},
        {QT_TR_NOOP("S"), QT_TR_NOOP("Storage")},
        {QT_TR_NOOP("A"), QT_TR_NOOP("Access Control")},
    };
    bool first = true;
    for (const auto &family : families) {
        auto *button = railButton(rail, tr(family.label), tr(family.tip));
        if (first) {
            button->setChecked(true);
            first = false;
        }
        railLayout->addWidget(button);
    }
    railLayout->addStretch(1);

    return rail;
}

QWidget *MainWindow::buildNavigator()
{
    auto *pane = new QWidget{this};
    auto *paneLayout = new QVBoxLayout{pane};
    paneLayout->setContentsMargins(6, 6, 6, 6);

    // The search narrows the tree, so it sits at the top of it (R-2).
    filterBox_ = new QLineEdit{pane};
    filterBox_->setPlaceholderText(tr("Search resources"));
    filterBox_->setClearButtonEnabled(true);
    paneLayout->addWidget(filterBox_);

    navigator_ = new QTreeWidget{pane};
    navigator_->setHeaderHidden(true);
    navigator_->setRootIsDecorated(true);
    navigator_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    buildTree();
    paneLayout->addWidget(navigator_, 1);

    return pane;
}

QWidget *MainWindow::buildTable()
{
    // A tree view, not a table one: the reference shows a flat tree, and the
    // widget draws a header over columns the same way without a proxy.
    view_ = new QTreeView{this};
    view_->setRootIsDecorated(false);
    view_->setItemsExpandable(false);
    view_->setAllColumnsShowFocus(true);
    view_->setModel(&model_);
    const ViewConfig config;
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setAlternatingRowColors(true);
    view_->header()->setStretchLastSection(true);
    view_->header()->setSectionResizeMode(QHeaderView::Interactive);
    view_->header()->setDefaultSectionSize(160);
    for (const ColumnWidth &column : config.columns()) {
        view_->setColumnWidth(static_cast<int>(column.role), static_cast<int>(column.width));
    }
    view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    return view_;
}

QWidget *MainWindow::buildDetail()
{
    auto *pane = new QFrame{this};
    pane->setFrameShape(QFrame::StyledPanel);
    pane->setMinimumWidth(DETAIL_WIDTH / 2);
    auto *paneLayout = new QVBoxLayout{pane};
    paneLayout->setContentsMargins(8, 8, 8, 8);

    auto *heading = new QLabel{tr("Detail"), pane};
    paneLayout->addWidget(heading);

    detail_ = new QLabel{tr("Select a row."), pane};
    detail_->setWordWrap(true);
    // A wrapping label must say so, or the layout gives it one line (LR-006).
    detail_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    detail_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    paneLayout->addWidget(detail_, 1);

    return pane;
}

void MainWindow::buildTree()
{
    // The families the API exposes, in the order the reference uses. A later
    // feature replaces this with discovery; the shape stays.
    auto *root = new QTreeWidgetItem{navigator_, QStringList{tr("Cluster")}};
    root->setExpanded(true);
    const struct {
        const char *label;
    } families[] = {
        {QT_TR_NOOP("Overview")},   {QT_TR_NOOP("Workloads")},  {QT_TR_NOOP("Config")},
        {QT_TR_NOOP("Network")},    {QT_TR_NOOP("Storage")},    {QT_TR_NOOP("Namespaces")},
        {QT_TR_NOOP("Events")},     {QT_TR_NOOP("Access Control")},
    };
    for (const auto &family : families) {
        static_cast<void>(new QTreeWidgetItem{root, QStringList{tr(family.label)}});
    }
}

void MainWindow::buildStatus(const QString &message)
{
    status_ = new QLabel{message, this};
    status_->setVisible(!message.isEmpty());
    statusBar()->addWidget(status_);
    statusBar()->setSizeGripEnabled(true);
}

QLabel *MainWindow::status() const noexcept { return status_; }

QLabel *MainWindow::region() const noexcept { return region_; }

QLineEdit *MainWindow::filterBox() const noexcept { return filterBox_; }

QComboBox *MainWindow::contextBox() const noexcept { return contextBox_; }

QComboBox *MainWindow::namespaceBox() const noexcept { return namespaceBox_; }

QTreeWidget *MainWindow::navigator() const noexcept { return navigator_; }

QTreeView *MainWindow::table() const noexcept { return view_; }

QSplitter *MainWindow::splitter() const noexcept { return splitter_; }

} // namespace antares::adapters::ui
