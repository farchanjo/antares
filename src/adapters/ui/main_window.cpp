#include "adapters/ui/main_window.hpp"

#include <QLabel>
#include <QListView>
#include <QVBoxLayout>
#include <QWidget>

#include "adapters/ui/resource_model.hpp"

namespace antares::adapters::ui {

MainWindow::MainWindow(ResourceModel &model, const QString &context, const QString &message,
                       QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(context.isEmpty() ? QStringLiteral("antares")
                                     : QStringLiteral("antares — ") + context);
    resize(720, 480);

    auto *central = new QWidget{this};
    auto *layout = new QVBoxLayout{central};

    // The configuration feature 009 owns (Batched, uniform rows, fixed widths)
    // is deliberately absent: this feature only has to prove the model reaches a
    // view, and a copied setting here would have to be kept in sync.
    auto *view = new QListView{central};
    view->setModel(&model);
    view->setUniformItemSizes(true);
    layout->addWidget(view);

    status_ = new QLabel{message, central};
    // An empty context is the normal first-run state, not an error, so the line
    // is hidden rather than shown blank.
    status_->setVisible(!message.isEmpty());
    layout->addWidget(status_);

    setCentralWidget(central);
}

QLabel *MainWindow::status() const noexcept { return status_; }

} // namespace antares::adapters::ui
