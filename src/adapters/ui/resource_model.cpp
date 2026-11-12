#include "adapters/ui/resource_model.hpp"

#include <span>

namespace antares::adapters::ui {

ResourceModel::ResourceModel(application::k8s::ObjectStore &store, domain::k8s::Scope scope,
                             QObject *parent)
    : QAbstractListModel(parent)
    , store_(store)
    , scope_(std::move(scope))
{
}

const application::k8s::StoreScope *ResourceModel::scopeRows() const
{
    return store_.find(scope_);
}

int ResourceModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    const auto *rows = scopeRows();
    return rows == nullptr ? 0 : static_cast<int>(rows->size());
}

QVariant ResourceModel::valueFor(const domain::k8s::Entry &entry,
                                 const domain::k8s::Scope &scope, int role)
{
    switch (role) {
    case NameRole:
    case Qt::DisplayRole:
        return QString::fromUtf8(entry.identity.name.c_str());
    case NamespaceRole:
        return QString::fromUtf8(scope.namespaceName.text().c_str());
    case StatusRole:
        return QString::fromUtf8(entry.status.reason.c_str());
    case AgeRole:
        // The projection keeps the timestamp, not a duration: the age is a
        // function of "now", and the model has no clock of its own.
        return QString::fromUtf8(entry.creationTimestamp.c_str());
    default:
        return {};
    }
}

QVariant ResourceModel::data(const QModelIndex &index, int role) const
{
    const auto *rows = scopeRows();
    if (rows == nullptr || !index.isValid() || index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= rows->size()) {
        return {};
    }
    return valueFor(rows->at(static_cast<std::size_t>(index.row())), scope_, role);
}

void ResourceModel::multiData(const QModelIndex &index, QModelRoleDataSpan roleData) const
{
    for (QModelRoleData &slot : roleData) {
        slot.setData(data(index, slot.role()));
    }
}

QHash<int, QByteArray> ResourceModel::roleNames() const
{
    return {{NameRole, "name"}, {NamespaceRole, "namespaceName"}, {StatusRole, "status"},
            {AgeRole, "age"}};
}

void ResourceModel::reload()
{
    beginResetModel();
    endResetModel();
}

} // namespace antares::adapters::ui
