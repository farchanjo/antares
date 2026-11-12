#pragma once

#include <QAbstractListModel>
#include <QModelRoleDataSpan>
#include <QObject>
#include <QString>
#include <QVariant>

#include "application/k8s/object_store.hpp"
#include "domain/k8s/scope.hpp"

namespace antares::adapters::ui {

/// The list model over an `ObjectStore` (FR-001 of 009).
///
/// It names the scope it shows and reads the rows through the store, so the
/// projection stays the only copy: the model never holds a document.
class ResourceModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role : int {
        NameRole = Qt::UserRole + 1,
        NamespaceRole,
        StatusRole,
        AgeRole,
    };

    ResourceModel(application::k8s::ObjectStore &store, domain::k8s::Scope scope,
                  QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    void multiData(const QModelIndex &index, QModelRoleDataSpan roleData) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Re-read the scope. A full reset; 009 narrows it to the changed rows.
    void reload();

    [[nodiscard]] static QVariant valueFor(const domain::k8s::Entry &entry,
                                           const domain::k8s::Scope &scope, int role);

private:
    [[nodiscard]] const application::k8s::StoreScope *scopeRows() const;

    application::k8s::ObjectStore &store_;
    domain::k8s::Scope scope_;
};

} // namespace antares::adapters::ui
