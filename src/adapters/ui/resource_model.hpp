#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QModelRoleDataSpan>
#include <QVariant>

#include "application/k8s/object_store.hpp"

namespace antares::adapters::ui {

/// The list model over an `ObjectStore` (FR-001 of 009).
///
/// The model is a lens: it holds a non-owning reference to the store and no row
/// data at all, so `rowCount()` is the store's size and `data()` re-reads the
/// entry. That is one copy instead of three.
class ResourceModel final : public QAbstractListModel {
    Q_OBJECT

public:
    /// The roles a delegate reads. They are the four columns of the row and
    /// nothing else; the mask an update carries is expressed in the same terms.
    enum Role : int {
        NameRole = Qt::UserRole + 1,
        NamespaceRole,
        StatusRole,
        AgeRole,
    };

    explicit ResourceModel(application::k8s::ObjectStore &store, QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;

    /// One call per row instead of one per role (FR-003 of 009). A delegate that
    /// calls `data()` directly still works, so both are implemented.
    void multiData(const QModelIndex &index, QModelRoleDataSpan roleData) const override;

    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Re-read the store after it changed. The view is fully reset on purpose:
    /// feature 009 replaces this with a `ChangeSet` mapped one-to-one, and every
    /// caller of this skeleton is already correct under the coarser contract.
    void reload();

private:
    [[nodiscard]] static QVariant valueFor(const application::k8s::Entry &entry, int role);

    application::k8s::ObjectStore &store_;
};

} // namespace antares::adapters::ui
