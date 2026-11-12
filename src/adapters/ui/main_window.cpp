#include "adapters/ui/main_window.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QTableView>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "adapters/ui/qt_list_model.hpp"
#include "adapters/ui/view_config.hpp"

namespace antares::adapters::ui {

MainWindow::MainWindow(QtListModel &model, const QString &context, const QString &message,
                       QWidget *parent)
    : QMainWindow(parent)
    , model_(model)
{
    // The context is data, not a translatable string, so it is placed with `%1`
    // and only the frame around it is translated (I18N-001).
    setWindowTitle(context.isEmpty() ? tr("antares") : tr("antares — %1").arg(context));
    resize(1100, 680);

    auto *central = new QWidget{this};
    auto *layout = new QVBoxLayout{central};
    // Spacing and margins come from the style (LR-007).
    layout->setContentsMargins({});

    buildBar();
    layout->addWidget(bar_);

    // Navigator on the left, rows on the right. The splitter is the only place
    // that decides how the width is shared, and the first pane keeps its size
    // when the window grows (LR-002).
    auto *splitter = new QSplitter{Qt::Horizontal, central};
    splitter->addWidget(buildNavigator());
    splitter->addWidget(buildTable());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setChildrenCollapsible(false);
    layout->addWidget(splitter, 1);

    status_ = new QLabel{message, central};
    // An empty context is the normal first-run state, not an error, so the line
    // is hidden rather than shown blank.
    status_->setVisible(!message.isEmpty());
    status_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(status_);

    setCentralWidget(central);
    // Focus starts on the rows, not on the filter (FR-008).
    view_->setFocus();
}

QWidget *MainWindow::buildNavigator()
{
    auto *pane = new QWidget{this};
    auto *paneLayout = new QVBoxLayout{pane};
    paneLayout->setContentsMargins(6, 6, 6, 6);

    // The search narrows the tree, so it sits at the top of it (reference).
    filterBox_ = new QLineEdit{pane};
    filterBox_->setPlaceholderText(tr("Search resources"));
    filterBox_->setClearButtonEnabled(true);
    paneLayout->addWidget(filterBox_);

    navigator_ = new QTreeWidget{pane};
    navigator_->setHeaderHidden(true);
    navigator_->setRootIsDecorated(true);
    // The tree is the only expanding child of the pane.
    navigator_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    paneLayout->addWidget(navigator_, 1);

    return pane;
}

QWidget *MainWindow::buildTable()
{
    view_ = new QTableView{this};
    view_->setModel(&model_);
    const ViewConfig config;
    view_->verticalHeader()->setVisible(false);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setShowGrid(false);
    view_->setAlternatingRowColors(true);
    // The last column takes the slack, never the first (LR-002).
    view_->horizontalHeader()->setStretchLastSection(true);
    view_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    view_->horizontalHeader()->setDefaultSectionSize(160);
    for (const ColumnWidth &column : config.columns()) {
        view_->setColumnWidth(static_cast<int>(column.role), static_cast<int>(column.width));
    }
    // The table is the only widget that takes the slack (LR-002).
    view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    return view_;
}

void MainWindow::buildBar()
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

    bar_->addWidget(scope);
}

QLabel *MainWindow::status() const noexcept { return status_; }

QComboBox *MainWindow::namespaceBox() const noexcept { return namespaceBox_; }

QComboBox *MainWindow::contextBox() const noexcept { return contextBox_; }

QLineEdit *MainWindow::filterBox() const noexcept { return filterBox_; }

QTreeWidget *MainWindow::navigator() const noexcept { return navigator_; }

} // namespace antares::adapters::ui
