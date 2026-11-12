#include "adapters/ui/resource_model.hpp"

#include <span>

namespace antares::adapters::ui {

namespace {

/// A row height, rendered without allocating a locale-aware formatter: the
/// column is diagnostics, not presentation.
[[nodiscard]] QString ageText(domain::Duration age)
{
    return QString::number(age.count()) + QStringLiteral("s");
}

} // namespace

ResourceModel::ResourceModel(application::k8s::ObjectStore &store, QObject *parent)
    : QAbstractListModel(parent)
    , store_(store)
{
}

int ResourceModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(store_.size());
}

QVariant ResourceModel::valueFor(const application::k8s::Entry &entry, int role)
{
    switch (role) {
    case NameRole:
    case Qt::DisplayRole:
        return QString::fromUtf8(entry.name.c_str());
    case NamespaceRole:
        return QString::fromUtf8(entry.namespaceName.c_str());
    case StatusRole:
        return QString::fromUtf8(entry.status.c_str());
    case AgeRole:
        return ageText(entry.age);
    default:
        return {};
    }
}

QVariant ResourceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= store_.size()) {
        return {};
    }
    return valueFor(store_.at(static_cast<std::size_t>(index.row())), role);
}

void ResourceModel::multiData(const QModelIndex &index, QModelRoleDataSpan roleData) const
{
    if (!index.isValid() || index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= store_.size()) {
        return;
    }
    const application::k8s::Entry &entry = store_.at(static_cast<std::size_t>(index.row()));
    for (QModelRoleData &slot : roleData) {
        slot.setData(valueFor(entry, slot.role()));
    }
}

QHash<int, QByteArray> ResourceModel::roleNames() const
{
    return {
        {NameRole, QByteArrayLiteral("name")},
        {NamespaceRole, QByteArrayLiteral("namespace")},
        {StatusRole, QByteArrayLiteral("status")},
        {AgeRole, QByteArrayLiteral("age")},
    };
}

void ResourceModel::reload()
{
    beginResetModel();
    endResetModel();
}

} // namespace antares::adapters::ui
