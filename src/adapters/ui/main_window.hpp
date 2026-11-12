#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

class QComboBox;
class QLabel;
class QLineEdit;
class QModelIndex;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QTreeView;
class QToolBar;
class QToolButton;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace antares::adapters::ui {

class QtListModel;

/// The window. Six regions, in the order the reference lays them out:
/// icon rail, navigator, toolbar, table, detail, status bar (R-1..R-6).
///
/// Every child sits in a layout (LR-001) and only the table expands (LR-002), so
/// the rows take the slack at any window size. The minimum comes from the layout
/// (LR-003). Every visible string goes through `tr()` (I18N-001).
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    /// `model` is not owned: the application keeps it and destroys the window
    /// first. `contexts` and `namespaces` fill the selectors; `message` is the
    /// reason the list is empty, if any.
    MainWindow(QtListModel &model, const QStringList &contexts, const QString &context,
               const QStringList &namespaces, const QString &message, QWidget *parent = nullptr);

    [[nodiscard]] QLabel *status() const noexcept;
    [[nodiscard]] QLabel *region() const noexcept;
    [[nodiscard]] QLineEdit *filterBox() const noexcept;
    [[nodiscard]] QComboBox *contextBox() const noexcept;
    [[nodiscard]] QComboBox *namespaceBox() const noexcept;
    [[nodiscard]] QTreeWidget *navigator() const noexcept;
    [[nodiscard]] QTreeView *table() const noexcept;
    [[nodiscard]] QTabWidget *content() const noexcept;
    [[nodiscard]] QSplitter *splitter() const noexcept;

private:
    void buildToolBar();
    QWidget *buildRail();
    QWidget *buildNavigator();
    QWidget *buildContent();
    QWidget *buildTablePage();
    QWidget *buildDetail();
    void buildTree();
    void openFamily(QTreeWidgetItem *item);
    void applyColumnWidths();
    void showDetail(const QModelIndex &index);
    void buildEmptyState();
    QWidget *buildRailAvatar();
    void buildStatus(const QString &message);
    void showEvent(QShowEvent *event) override;

    QtListModel &model_;
    QToolBar *bar_ = nullptr;
    QComboBox *contextBox_ = nullptr;
    QComboBox *namespaceBox_ = nullptr;
    QComboBox *kindBox_ = nullptr;
    QLineEdit *filterBox_ = nullptr;
    QTreeWidget *navigator_ = nullptr;
    QTreeView *view_ = nullptr;
    QLabel *detail_ = nullptr;
    QLabel *status_ = nullptr;
    QLabel *region_ = nullptr;
    QLabel *count_ = nullptr;
    QLabel *emptyState_ = nullptr;
    QTabWidget *content_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QSplitter *splitter_ = nullptr;
};

} // namespace antares::adapters::ui
