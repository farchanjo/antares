#include "adapters/ui/main_window.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QCursor>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QStatusBar>
#include <QStyle>
#include <QResizeEvent>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QtWidgets/QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <QWidget>

#include "adapters/ui/avatar.hpp"
#include "adapters/ui/detail_pane.hpp"
#include "adapters/ui/edit_dialog.hpp"
#include "adapters/ui/preferences_page.hpp"
#include "adapters/ui/proportional_header.hpp"
#include "adapters/ui/row_delegate.hpp"
#include "domain/k8s/options.hpp"
#include "ports/k8s/kube_client.hpp"
#include "adapters/ui/family.hpp"
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

MainWindow::MainWindow(QtListModel &model, ports::k8s::KubeClient *writer, const QStringList &contexts,
                       const QString &context, const QStringList &namespaces, const QString &message,
                       QWidget *parent)
    : QMainWindow(parent)
    , model_(model)
    , writer_(writer)
{
    setWindowTitle(context.isEmpty() ? tr("antares") : tr("antares — %1").arg(context));
    resize(1640, 980);

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
    // Choosing a namespace re-scopes the list; the empty data is "all".
    QObject::connect(namespaceBox_, &QComboBox::currentIndexChanged, this, [this](int) {
        domain::k8s::Scope scope = model_.scope();
        const std::string chosen = namespaceBox_->currentData().toString().toUtf8().constData();
        if (auto parsed = domain::k8s::NamespaceName::of(chosen); parsed) {
            scope.namespaceName = *parsed;
        }
        model_.setScope(std::move(scope));
    });
    layout->addWidget(bar_);

    splitter_ = new QSplitter{Qt::Horizontal, central};
    splitter_->addWidget(buildRail());
    splitter_->addWidget(buildNavigator());
    splitter_->addWidget(buildContent());
    splitter_->addWidget(buildDetail());
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 0);
    splitter_->setStretchFactor(2, 1);
    splitter_->setStretchFactor(3, 0);
    splitter_->setChildrenCollapsible(false);
    splitter_->setSizes({RAIL_WIDTH, NAVIGATOR_WIDTH, 900, DETAIL_WIDTH});
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
    auto *reload = iconButton(bar_, QStyle::SP_BrowserReload, tr("Reload"));
    QObject::connect(reload, &QToolButton::clicked, this, [this] { model_.refresh(); });
    bar_->addWidget(back);
    bar_->addWidget(forward);
    bar_->addWidget(reload);

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
    // Typing narrows the model; the model owns the filter so a repaint never
    // re-scans.
    QObject::connect(filterBox_, &QLineEdit::textChanged, this,
                     [this](const QString &text) { model_.setFilter(text.toUtf8().constData()); });
    searchLayout->addWidget(filterBox_, 1);
    paneLayout->addWidget(searchRow);

    navigator_ = new QTreeWidget{pane};
    navigator_->setHeaderHidden(true);
    navigator_->setRootIsDecorated(true);
    navigator_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    buildTree();
    // A click on a family opens its table; the tree owns the family index.
    QObject::connect(navigator_, &QTreeWidget::itemClicked, this,
                     [this](QTreeWidgetItem *item, int) { openFamily(item); });
    paneLayout->addWidget(navigator_, 1);

    return pane;
}

QWidget *MainWindow::buildContent()
{
    content_ = new QTabWidget{this};
    content_->setDocumentMode(true);
    content_->setTabsClosable(true);
    content_->setMovable(true);
    // The tab strip is the reference's, and its first tab is the start page:
    // the watermark while nothing is open, the rows once a family is chosen.
    // Preferences is a tab of its own and comes first, as the reference opens it.
    content_->addTab(new PreferencesPage{content_}, tr("Preferences"));
    tableTab_ = content_->addTab(buildTablePage(), tr("Start"));
    return content_;
}

QWidget *MainWindow::buildTablePage()
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
    auto *header = new ProportionalHeader{view_};
    view_->setHeader(header);
    delegate_ = new RowDelegate{model_, this};
    view_->setItemDelegate(delegate_);
    // A reset recreates the header sections at the default width; the widths are
    // a property of the family, so they are re-applied whenever it changes.
    QObject::connect(&model_, &QAbstractItemModel::modelReset, this,
                     [this] { applyColumnWidths(); });
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setAlternatingRowColors(true);
    // The name column absorbs the space and the rest keep their natural width,
    // so the trailing menu stays reachable without a horizontal scroll.

    view_->header()->setMinimumSectionSize(60);
    view_->header()->setDefaultSectionSize(160);
    applyColumnWidths();
    // The detail pane follows the row under the cursor, so a click answers
    // "what is this" without a second gesture.
    QObject::connect(view_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
                     [this](const QModelIndex &current, const QModelIndex &) {
                         showDetail(current);
                     });
    // The trailing column opens the row menu; every other click keeps the
    // selection behaviour the view already has.
    QObject::connect(view_, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        const std::span<const RowRole> columns = familyColumns(model_.family());
        if (!index.isValid() || index.column() < 0 ||
            index.column() >= static_cast<int>(columns.size())) {
            return;
        }
        if (columns[static_cast<std::size_t>(index.column())] == RowRole::name) {
            model_.toggleTick(static_cast<std::uint32_t>(index.row()));
            return;
        }
        if (columns[static_cast<std::size_t>(index.column())] != RowRole::actions) {
            return;
        }
        QMenu menu{this};
        const QString name = index.sibling(index.row(), 0).data().toString();
        menu.addAction(tr("Edit"), this, [this, name] { editObject(name); });
        menu.addAction(tr("Delete"), this, [this, name] { removeObject(name); });
        menu.addSeparator();
        menu.addAction(tr("Copy name"), this, [this, index] {
            QApplication::clipboard()->setText(index.sibling(index.row(), 0).data().toString());
        });
        menu.exec(QCursor::pos());
    });
    view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    emptyState_ = new QLabel{tr("No objects in this scope."), stack};
    emptyState_->setAlignment(Qt::AlignCenter);
    emptyState_->setWordWrap(true);

    stack->addWidget(view_);
    stack->addWidget(emptyState_);
    stack->setCurrentWidget(view_);
    pages_ = stack;
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

    detail_ = new DetailPane{pane};
    // The close action collapses the pane; the splitter remembers the width.
    QObject::connect(detail_, &DetailPane::closed, this, [this] {
        detail_->setVisible(false);
        splitter_->setSizes({RAIL_WIDTH, NAVIGATOR_WIDTH, 1, 0});
    });
    paneLayout->addWidget(detail_, 1);

    return pane;
}

void MainWindow::buildTree()
{
    navigator_->setColumnCount(2);
    navigator_->header()->setStretchLastSection(false);
    navigator_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    navigator_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    // A section header: caps, small, not selectable.
    const auto section = [this](const QString &label) {
        auto *item = new QTreeWidgetItem{navigator_, QStringList{label.toUpper()}};
        item->setFirstColumnSpanned(true);
        item->setFlags(Qt::ItemIsEnabled);
        QFont font = item->font(0);
        font.setBold(true);
        font.setPixelSize(10);
        item->setFont(0, font);
        return item;
    };

    const struct {
        const char *label;
        bool isNew;
        Family family;
    } families[] = {
        {QT_TR_NOOP("Overview"), false, Family::pods},
        {QT_TR_NOOP("Applications"), false, Family::deployments},
        {QT_TR_NOOP("Argo CD"), true, Family::deployments},
        {QT_TR_NOOP("Nodes"), false, Family::nodes},
        {QT_TR_NOOP("Workloads"), false, Family::pods},
        {QT_TR_NOOP("Config"), false, Family::configMaps},
        {QT_TR_NOOP("Network"), false, Family::services},
        {QT_TR_NOOP("Storage"), false, Family::persistentVolumeClaims},
        {QT_TR_NOOP("Device Resources"), true, Family::customResources},
        {QT_TR_NOOP("Namespaces"), false, Family::namespaces},
        {QT_TR_NOOP("Events"), false, Family::events},
        {QT_TR_NOOP("Helm"), false, Family::customResources},
        {QT_TR_NOOP("Access Control"), false, Family::roles},
        {QT_TR_NOOP("Custom Resources"), false, Family::customResources},
        {QT_TR_NOOP("Security Center"), false, Family::customResources},
    };

    // A cluster expands to the families; the badge sits in the second column.
    const auto cluster = [&families](QTreeWidgetItem *parent, const QString &name) {
        auto *item = new QTreeWidgetItem{parent, QStringList{name}};
        for (const auto &family : families) {
            auto *child = new QTreeWidgetItem{item, QStringList{tr(family.label)}};
            child->setData(0, Qt::UserRole + 1, static_cast<int>(family.family));
            if (family.isNew) {
                child->setText(1, tr("NEW"));
            }
        }
        return item;
    };

    auto *clusters = section(tr("Kubernetes clusters"));
    auto *aks = new QTreeWidgetItem{clusters, QStringList{tr("AKS")}};
    static_cast<void>(aks);
    auto *eks = new QTreeWidgetItem{clusters, QStringList{tr("EKS")}};
    static_cast<void>(eks);

    auto *local = new QTreeWidgetItem{clusters, QStringList{tr("Local Kubeconfigs")}};
    for (int index = 0; index < contextBox_->count(); ++index) {
        static_cast<void>(cluster(local, contextBox_->itemText(index)));
    }

    auto *teamwork = section(tr("Teamwork"));
    static_cast<void>(teamwork);

    navigator_->expandAll();
}

void MainWindow::editObject(const QString &name)
{
    if (writer_ == nullptr || name.isEmpty()) {
        return;
    }
    domain::k8s::ApiPath path{model_.scope().type, std::nullopt, std::nullopt};
    if (!model_.scope().namespaceName.empty()) {
        path.namespaceName = model_.scope().namespaceName.text();
    }
    const auto named = path.named(name.toUtf8().constData());
    if (!named) {
        return;
    }
    // The document is fetched first: the editor shows the server's own body, not
    // a projection the client guessed.
    const auto submitted = writer_->get(*named, [this, name, path = *named](
                                                     ports::k8s::ObjectResult result) {
        if (!result) {
            QMessageBox::warning(nullptr, tr("Edit"), tr("The object could not be read."));
            return;
        }
        auto *dialog = new EditDialog{tr("Edit %1").arg(name),
                                      QString::fromUtf8(result->body.dump().c_str()), this};
        if (dialog->exec() != QDialog::Accepted) {
            dialog->deleteLater();
            return;
        }
        const std::string body = dialog->body().toUtf8().constData();
        dialog->deleteLater();
        static_cast<void>(writer_->patch(
            path, domain::k8s::PatchType::merge, body, std::nullopt,
            [this](ports::k8s::ObjectResult applied) {
                if (!applied) {
                    QMessageBox::warning(nullptr, tr("Edit"), tr("The server refused the change."));
                    return;
                }
                model_.refresh();
            }));
    });
    if (!submitted) {
        QMessageBox::warning(this, tr("Edit"), tr("The request could not be sent."));
    }
}

void MainWindow::removeObject(const QString &name)
{
    if (writer_ == nullptr || name.isEmpty()) {
        return;
    }
    if (QMessageBox::question(this, tr("Delete"),
                              tr("Delete %1? This cannot be undone.").arg(name)) !=
        QMessageBox::Yes) {
        return;
    }
    domain::k8s::ApiPath path{model_.scope().type, std::nullopt, std::nullopt};
    if (!model_.scope().namespaceName.empty()) {
        path.namespaceName = model_.scope().namespaceName.text();
    }
    const auto named = path.named(name.toUtf8().constData());
    if (!named) {
        return;
    }
    const auto submitted = writer_->remove(
        *named, domain::k8s::DeleteOptions{}, [this](ports::k8s::StatusResult result) {
            if (!result) {
                QMessageBox::warning(nullptr, tr("Delete"), tr("The server refused the delete."));
                return;
            }
            model_.refresh();
        });
    if (!submitted) {
        QMessageBox::warning(this, tr("Delete"), tr("The request could not be sent."));
    }
}

void MainWindow::refreshCount()
{
    // `QString::arg` drags in a metatype converter that the libstdc++-built Qt
    // cannot resolve against this libc++ tree, so the number is glued.
    const QString shown = QString::number(model_.rowCount(QModelIndex{}));
    const std::size_t ticked = model_.tickedCount();
    count_->setText(ticked == 0 ? tr("items: ") + shown
                                : tr("selected: ") + QString::number(static_cast<int>(ticked)) +
                                      tr(" of ") + shown);
}

void MainWindow::showDetail(const QModelIndex &index)
{
    if (!index.isValid()) {
        detail_->clear();
        return;
    }
    detail_->setVisible(true);
    // Every column of the row, labelled, so the pane is useful without a
    // per-family layout.
    const std::span<const RowRole> columns = familyColumns(model_.family());
    QStringList lines;
    for (std::size_t at = 0; at < columns.size(); ++at) {
        const QString value =
            model_.data(model_.index(index.row(), static_cast<int>(at)), Qt::DisplayRole).toString();
        if (value.isEmpty()) {
            continue;
        }
        lines.append(QStringLiteral("%1: %2")
                         .arg(QString::fromUtf8(rowRoleLabel(columns[at])), value));
    }
    detail_->show(QString::fromUtf8(familyLabel(model_.family())),
                  model_.data(model_.index(index.row(), 0), Qt::DisplayRole).toString(), lines);
}

void MainWindow::openFamily(QTreeWidgetItem *item)
{
    if (item == nullptr) {
        return;
    }
    const QVariant index = item->data(0, Qt::UserRole + 1);
    if (!index.isValid()) {
        // A section or a cluster: nothing to open.
        return;
    }
    const auto family = static_cast<Family>(index.toInt());
    model_.setFamily(family);
    detail_->clear();
    // The tab is named `<Family> - <cluster>`, like the reference.
    const QString label = QString::fromUtf8(familyLabel(family));
    const QString title = tr("%1 - %2").arg(label, contextBox_->currentText());
    content_->setTabText(tableTab_, title);
    content_->setCurrentIndex(tableTab_);
}

void MainWindow::buildStatus(const QString &message)
{
    // Two counters on the left of the reference; the row count is the one this
    // build can answer.
    count_ = new QLabel{this};
    statusBar()->addWidget(count_);

    QObject::connect(&model_, &QtListModel::ticksChanged, this, &MainWindow::refreshCount);
    QObject::connect(&model_, &QtListModel::rowsChanged, this, &MainWindow::refreshCount);
    refreshCount();

    status_ = new QLabel{message, this};
    status_->setVisible(!message.isEmpty());
    statusBar()->addWidget(status_, 1);

    auto *support = new QLabel{tr("Support"), this};
    statusBar()->addPermanentWidget(support);
    statusBar()->setSizeGripEnabled(true);
}

void MainWindow::applyColumnWidths()
{
    // A role is a stable id, not a position: the weight is looked up by role and
    // handed to the header at the column index the family gives it. The saved
    // width is the weight, so a column the reader widened keeps the larger share.
    auto *header = dynamic_cast<ProportionalHeader *>(view_->header());
    if (header == nullptr) {
        return;
    }
    const ViewConfig config;
    header->clearWeights();
    const std::span<const RowRole> shown = familyColumns(model_.family());
    for (std::size_t at = 0; at < shown.size(); ++at) {
        const RowRole role = shown[at];
        const auto wanted = std::find_if(
            config.columns().begin(), config.columns().end(),
            [role](const ColumnWidth &column) { return column.role == role; });
        const int weight = wanted == config.columns().end() ? 100 : static_cast<int>(wanted->width);
        header->setWeight(static_cast<int>(at), weight, role == RowRole::actions ? 28 : 48);
    }
    header->shareNow(view_->viewport()->width());
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // Deferred past the layout pass: the splitter has not sized the pane yet when
    // this handler runs, so measuring now would share the old width.
    QTimer::singleShot(0, this, [this] { applyColumnWidths(); });
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    // After the first layout pass, so the sections exist at their final count.
    QTimer::singleShot(0, this, [this] {
        applyColumnWidths();
        std::fprintf(stderr, "  [pos] n=%d w5=%d\n", view_->header()->count(),
                     view_->columnWidth(5));
    });
}

QLabel *MainWindow::status() const noexcept { return status_; }

QLabel *MainWindow::region() const noexcept { return region_; }

QLineEdit *MainWindow::filterBox() const noexcept { return filterBox_; }

QComboBox *MainWindow::contextBox() const noexcept { return contextBox_; }

QComboBox *MainWindow::namespaceBox() const noexcept { return namespaceBox_; }

QTreeWidget *MainWindow::navigator() const noexcept { return navigator_; }

QTreeView *MainWindow::table() const noexcept { return view_; }

QTabWidget *MainWindow::content() const noexcept { return content_; }

QSplitter *MainWindow::splitter() const noexcept { return splitter_; }

} // namespace antares::adapters::ui
