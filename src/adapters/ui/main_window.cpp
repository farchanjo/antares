#include "adapters/ui/main_window.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include "adapters/ui/qt_list_model.hpp"

namespace antares::adapters::ui {

MainWindow::MainWindow(QtListModel &model, const QString &context, const QString &message,
                       QWidget *parent)
    : QMainWindow(parent)
    , model_(model)
{
    // The context is data, not a translatable string, so it is placed with `%1`
    // and only the frame around it is translated (I18N-001).
    setWindowTitle(context.isEmpty() ? tr("antares") : tr("antares — %1").arg(context));
    resize(960, 600);

    auto *central = new QWidget{this};
    auto *layout = new QVBoxLayout{central};
    // Spacing and margins come from the style (LR-007).
    layout->setContentsMargins({});

    buildBar();
    layout->addWidget(bar_);

    view_ = new QListView{central};
    view_->setModel(&model_);
    view_->setUniformItemSizes(true);
    // The list is the only widget that takes the slack (LR-002).
    view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(view_, 1);

    status_ = new QLabel{message, central};
    // An empty context is the normal first-run state, not an error, so the line
    // is hidden rather than shown blank.
    status_->setVisible(!message.isEmpty());
    status_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(status_);

    setCentralWidget(central);
    // Focus starts on the rows, not on the first control (FR-008).
    view_->setFocus();
}

void MainWindow::buildBar()
{
    bar_ = new QToolBar{this};
    bar_->setMovable(false);
    bar_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto *scope = new QWidget{bar_};
    auto *scopeLayout = new QHBoxLayout{scope};
    scopeLayout->setContentsMargins({});

    auto *namespaceLabel = new QLabel{tr("&Namespace"), scope};
    namespaceBox_ = new QComboBox{scope};
    namespaceLabel->setBuddy(namespaceBox_);
    namespaceBox_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    scopeLayout->addWidget(namespaceLabel);
    scopeLayout->addWidget(namespaceBox_);

    auto *filterLabel = new QLabel{tr("&Filter"), scope};
    filterBox_ = new QLineEdit{scope};
    filterLabel->setBuddy(filterBox_);
    // A filter is a substring, so it is clearable and needs no completer.
    filterBox_->setClearButtonEnabled(true);
    filterBox_->setMinimumWidth(filterBox_->sizeHint().width() * 2);
    scopeLayout->addWidget(filterLabel);
    scopeLayout->addWidget(filterBox_);

    bar_->addWidget(scope);
}

QLabel *MainWindow::status() const noexcept { return status_; }

QComboBox *MainWindow::namespaceBox() const noexcept { return namespaceBox_; }

QLineEdit *MainWindow::filterBox() const noexcept { return filterBox_; }

} // namespace antares::adapters::ui
