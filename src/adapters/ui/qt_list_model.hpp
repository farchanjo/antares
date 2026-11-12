#pragma once

#include <QObject> // IWYU pragma: keep — Q_OBJECT needs it

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "adapters/ui/model_sink.hpp"
#include "adapters/ui/model_wakeup.hpp"
#include "adapters/ui/resource_list_model.hpp"
#include "adapters/ui/family.hpp"
#include "domain/k8s/scope.hpp"

#if defined(ANTARES_HAS_QT)

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QModelRoleDataSpan>
#include <QVariant>

namespace antares::adapters::ui {

/// The one class that knows Qt exists.
///
/// Everything else in `adapters/ui` is plain C++, because `Qt6Core` on this
/// host is built against libstdc++ while the tree links libc++: a
/// `QString`-returning Qt call is an undefined symbol, so the model cannot hold
/// a `QVariant` and the suite cannot instantiate a widget. This adapter touches
/// only the Qt surface that takes and returns Qt types, which links.
///
/// It is both the sink and the wakeup of the model it owns: the model notifies
/// this class, and this class wraps each notification in the matching
/// `begin*`/`end*` pair. The store is already in its post-batch state when a
/// notification arrives (see `ModelSink`), which is safe because Qt carries the
/// indices as arguments.
class QtListModel : public QAbstractListModel, public ModelSink, public ModelWakeup {
    Q_OBJECT

public:
    QtListModel(application::k8s::ObjectStore &store, domain::k8s::Scope scope, PageSource &pages,
                ports::log::Logger *logger = nullptr, QObject *parent = nullptr);
    ~QtListModel() override;

    /// Forward a store batch to the model below.
    void observe(const domain::k8s::ChangeSet &changes);

    /// Which family the table shows. Its columns are the family's.
    void setFamily(Family family);

    /// Narrow the rows to those whose name contains `needle`.
    void setFilter(const std::string &needle);

    /// Point the list at another scope and reload it.
    void setScope(domain::k8s::Scope scope);
    [[nodiscard]] const domain::k8s::Scope &scope() const noexcept;

    /// Drop the rows and ask for the first page again.
    void refresh();
    [[nodiscard]] Family family() const noexcept { return family_; }

    // No default arguments here: the base declares them and re-specifying a
    // default on an override is what `google-default-arguments` refuses.
    [[nodiscard]] int rowCount(const QModelIndex &parent) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    /// Drop the retry row and ask again.
    void retry();

    // ModelSink — the rows are already in the store.
    void rowsReset() override;
    void rowsInserted(std::uint32_t first, std::uint32_t last) override;
    void rowsRemoved(std::uint32_t first, std::uint32_t last) override;
    void rowsMoved(std::uint32_t first, std::uint32_t last,
                   std::uint32_t destinationChild) override;
    void dataChanged(std::uint32_t first, std::uint32_t last,
                     std::span<const RowRole> roles) override;

    // ModelWakeup — hop to the thread this object lives on.
    void wake() override;

    [[nodiscard]] ResourceListModel &model() noexcept { return *model_; }

private:
    std::unique_ptr<ResourceListModel> model_;
    Family family_ = Family::pods;
};

} // namespace antares::adapters::ui

#endif // ANTARES_HAS_QT
