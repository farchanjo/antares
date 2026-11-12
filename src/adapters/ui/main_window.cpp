#include "adapters/ui/main_window.hpp"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "adapters/ui/avatar.hpp"
#include "adapters/ui/qt_list_model.hpp"
#include "adapters/ui/view_config.hpp"

namespace antares::adapters::ui {

namespace {

constexpr int RAIL_WIDTH = 48;
constexpr int NAVIGATOR_WIDTH = 240;
constexpr int DETAIL_WIDTH = 360;
constexpr int TOOL_HEIGHT = 22;

/// An icon-only tool button, disabled when the action has nowhere to go.
QToolButton *iconButton(QWidget *parent, QStyle::StandardPixmap icon, const QString &tip)
{
    auto *button = new QToolButton{parent};
    button->setIcon(parent->style()->standardIcon(icon));
    button->setToolTip(tip);
    button->setAutoRaise(true);
    button->setFixedSize(TOOL_HEIGHT, TOOL_HEIGHT);
    return button;
}

} // namespace

MainWindow::MainWindow(QtListModel &model, const QStringList &contexts, const QString &context,
                       const QStringList &namespaces, const QString &message, QWidget *parent)
    : QMainWindow(parent)
    , model_(model)
{
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

    // History first, on the left, like the reference.
    auto *back = iconButton(bar_, QStyle::SP_ArrowBack, tr("Back"));
    back->setEnabled(false);
    auto *forward = iconButton(bar_, QStyle::SP_ArrowForward, tr("Forward"));
    forward->setEnabled(false);
    bar_->addWidget(back);
    bar_->addWidget(forward);

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

    // The right-hand cluster of the reference: AI, overflow, notifications, me.
    auto *ai = new QToolButton{bar_};
    ai->setText(tr("Configure AI"));
    ai->setAutoRaise(true);
    ai->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    ai->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    bar_->addWidget(ai);

    auto *more = new QToolButton{bar_};
    more->setText(QStringLiteral("\u25BE"));
    more->setAutoRaise(true);
    bar_->addWidget(more);

    auto *notify = new QToolButton{bar_};
    notify->setText(tr("notifications"));
    notify->setAutoRaise(true);
    notify->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    notify->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
    bar_->addWidget(notify);

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
    railLayout->setSpacing(6);

    // One avatar per cluster, then the add action. A rail entry is a cluster,
    // not a resource family.
    const QStringList clusters = contextBox_->count() > 0
                                     ? QStringList{contextBox_->currentText()}
                                     : QStringList{tr("local")};
    bool first = true;
    for (const QString &cluster : clusters) {
        auto *avatar = new Avatar{cluster, rail};
        if (first) {
            avatar->setChecked(true);
            first = false;
        }
        railLayout->addWidget(avatar, 0, Qt::AlignHCenter);
    }

    auto *add = new QToolButton{rail};
    add->setText(QStringLiteral("+"));
    add->setToolTip(tr("Add cluster"));
    add->setAutoRaise(true);
    railLayout->addWidget(add, 0, Qt::AlignHCenter);

    railLayout->addStretch(1);

    auto *terminal = new QToolButton{rail};
    terminal->setText(tr(">_"));
    terminal->setToolTip(tr("Terminal"));
    terminal->setAutoRaise(true);
    railLayout->addWidget(terminal, 0, Qt::AlignHCenter);

    auto *settings = new QToolButton{rail};
    settings->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    settings->setToolTip(tr("Settings"));
    settings->setAutoRaise(true);
    railLayout->addWidget(settings, 0, Qt::AlignHCenter);

    return rail;
}

QWidget *MainWindow::buildNavigator()
{
    auto *pane = new QWidget{this};
    auto *paneLayout = new QVBoxLayout{pane};
    paneLayout->setContentsMargins(6, 6, 6, 6);
    paneLayout->setSpacing(4);

    auto *title = new QLabel{tr("Navigator"), pane};
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    paneLayout->addWidget(title);

    // The search holds the cluster name; the two icons are the filter and the
    // magnifier of the reference.
    auto *searchRow = new QWidget{pane};
    auto *searchLayout = new QHBoxLayout{searchRow};
    searchLayout->setContentsMargins({});
    searchLayout->setSpacing(2);
    filterBox_ = new QLineEdit{searchRow};
    filterBox_->setPlaceholderText(tr("Search resources"));
    filterBox_->setClearButtonEnabled(true);
    searchLayout->addWidget(filterBox_, 1);
    paneLayout->addWidget(searchRow);

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
    // The empty state sits in a stack with the rows, so a scope with nothing in
    // it says why instead of showing a blank grid.
    auto *stack = new QStackedWidget{this};

    view_ = new QTreeView{stack};
    view_->setRootIsDecorated(false);
    view_->setItemsExpandable(false);
    view_->setAllColumnsShowFocus(true);
    view_->setUniformRowHeights(true);
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

    emptyState_ = new QLabel{tr("No objects in this scope."), stack};
    emptyState_->setAlignment(Qt::AlignCenter);
    emptyState_->setWordWrap(true);

    stack->addWidget(view_);
    stack->addWidget(emptyState_);
    stack->setCurrentWidget(view_);
    return stack;
}

QWidget *MainWindow::buildDetail()
{
    auto *pane = new QFrame{this};
    pane->setFrameShape(QFrame::StyledPanel);
    pane->setMinimumWidth(DETAIL_WIDTH / 2);
    auto *paneLayout = new QVBoxLayout{pane};
    paneLayout->setContentsMargins(8, 8, 8, 8);

    auto *heading = new QLabel{tr("Detail"), pane};
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);
    paneLayout->addWidget(heading);

    detail_ = new QLabel{tr("Select a row."), pane};
    detail_->setWordWrap(true);
    detail_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    detail_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    detail_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    paneLayout->addWidget(detail_, 1);

    return pane;
}

void MainWindow::buildTree()
{
    auto *root = new QTreeWidgetItem{navigator_, QStringList{tr("Cluster")}};
    root->setExpanded(true);
    const struct {
        const char *label;
        bool isNew;
    } families[] = {
        {QT_TR_NOOP("Overview"), false},   {QT_TR_NOOP("Applications"), false},
        {QT_TR_NOOP("Argo CD"), true},     {QT_TR_NOOP("Nodes"), false},
        {QT_TR_NOOP("Workloads"), false},  {QT_TR_NOOP("Config"), false},
        {QT_TR_NOOP("Network"), false},    {QT_TR_NOOP("Storage"), false},
        {QT_TR_NOOP("Device Resources"), true}, {QT_TR_NOOP("Namespaces"), false},
        {QT_TR_NOOP("Events"), false},     {QT_TR_NOOP("Helm"), false},
        {QT_TR_NOOP("Access Control"), false}, {QT_TR_NOOP("Custom Resources"), false},
        {QT_TR_NOOP("Security Center"), false},
    };
    for (const auto &family : families) {
        auto *item = new QTreeWidgetItem{root, QStringList{tr(family.label)}};
        if (family.isNew) {
            item->setText(1, tr("NEW"));
        }
    }
    navigator_->setColumnCount(2);
    navigator_->header()->setStretchLastSection(false);
    navigator_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    navigator_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
}

void MainWindow::buildStatus(const QString &message)
{
    // Two counters on the left of the reference; the row count is the one this
    // build can answer.
    count_ = new QLabel{QStringLiteral("0"), this};
    statusBar()->addWidget(count_);

    status_ = new QLabel{message, this};
    status_->setVisible(!message.isEmpty());
    statusBar()->addWidget(status_, 1);

    auto *support = new QLabel{tr("Support"), this};
    statusBar()->addPermanentWidget(support);
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
