#include "adapters/ui/qt_list_model.hpp"

#include <cstdio>

#if defined(ANTARES_HAS_QT)

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include <QMetaObject>
#include <QString>

#include "adapters/ui/row_role.hpp"

namespace antares::adapters::ui {

namespace {

[[nodiscard]] QString toQt(const std::string &text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

QtListModel::QtListModel(application::k8s::ObjectStore &store, domain::k8s::Scope scope,
                         PageSource &pages, ports::log::Logger *logger, QObject *parent)
    : QAbstractListModel(parent),
      model_(std::make_unique<ResourceListModel>(store, std::move(scope), pages, *this, *this,
                                                 PageToken{}, logger))
{
}

QtListModel::~QtListModel() = default;

int QtListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    const int rows = static_cast<int>(model_->rowCount());
    std::fprintf(stderr, "  [rowCount] %d\n", rows);
    return rows;
}

QVariant QtListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    // A view may ask by role (the registered 257..260) or, as a table view does
    // for the default role, by column. Only `DisplayRole` takes the column: any
    // other role is answered by a registered one, or not at all — returning text
    // for `SizeHintRole` gives the row a zero height and nothing draws.
    std::optional<RowRole> wanted = roleForIndex(static_cast<std::uint32_t>(role));
    const std::span<const RowRole> columns = familyColumns(family_);
    if (!wanted && role == Qt::DisplayRole && index.column() >= 0 &&
        index.column() < static_cast<int>(columns.size())) {
        wanted = columns[static_cast<std::size_t>(index.column())];
    }
    if (!wanted) {
        return {};
    }
    const auto raw = model_->data(static_cast<std::uint32_t>(index.row()), *wanted);
    const std::string text = raw.value_or(std::string{});
    return QVariant{toQt(text)};
}

QHash<int, QByteArray> QtListModel::roleNames() const
{
    QHash<int, QByteArray> names;
    for (const RowRole role : {RowRole::name, RowRole::namespaceName, RowRole::status, RowRole::age}) {
        const std::string_view text = rowRoleName(role);
        names.insert(static_cast<int>(roleIndex(role)), QByteArray{text.data(), static_cast<qsizetype>(text.size())});
    }
    return names;
}

bool QtListModel::canFetchMore(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return false;
    }
    return model_->canFetchMore();
}

void QtListModel::observe(const domain::k8s::ChangeSet &changes) { model_->observe(changes); }

void QtListModel::fetchMore(const QModelIndex &parent)
{
    if (parent.isValid()) {
        return;
    }
    model_->fetchMore();
}

void QtListModel::retry()
{
    model_->retry();
}

void QtListModel::rowsReset()
{
    beginResetModel();
    endResetModel();
}

void QtListModel::rowsInserted(std::uint32_t first, std::uint32_t last)
{
    beginInsertRows(QModelIndex(), static_cast<int>(first), static_cast<int>(last));
    endInsertRows();
}

void QtListModel::rowsRemoved(std::uint32_t first, std::uint32_t last)
{
    beginRemoveRows(QModelIndex(), static_cast<int>(first), static_cast<int>(last));
    endRemoveRows();
}

void QtListModel::rowsMoved(std::uint32_t first, std::uint32_t last,
                            std::uint32_t destinationChild)
{
    beginMoveRows(QModelIndex(), static_cast<int>(first), static_cast<int>(last), QModelIndex(),
                  static_cast<int>(destinationChild));
    endMoveRows();
}

int QtListModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(familyColumns(family_).size());
}

void QtListModel::refresh()
{
    ticked_.clear();
    model_->refresh();
}

bool QtListModel::isTicked(std::uint32_t row) const
{
    return ticked_.contains(row);
}

void QtListModel::toggleTick(std::uint32_t row)
{
    if (ticked_.erase(row) == 0) {
        ticked_.insert(row);
    }
    const QModelIndex at = index(static_cast<int>(row), 0);
    Q_EMIT QAbstractListModel::dataChanged(at, at, {});
}

void QtListModel::clearTicks()
{
    if (ticked_.empty()) {
        return;
    }
    ticked_.clear();
    Q_EMIT QAbstractListModel::dataChanged(index(0, 0), index(rowCount(QModelIndex{}) - 1, 0), {});
}

void QtListModel::setScope(domain::k8s::Scope scope) { model_->setScope(std::move(scope)); }

const domain::k8s::Scope &QtListModel::scope() const noexcept { return model_->scope(); }

void QtListModel::setFilter(const std::string &needle)
{
    model_->setFilter(needle);
}

void QtListModel::setFamily(Family family)
{
    if (family == family_) {
        return;
    }
    beginResetModel();
    family_ = family;
    ticked_.clear();
    // The scope carries the resource: the list must ask the new one.
    domain::k8s::Scope scope = model_->scope();
    scope.type = domain::k8s::GroupVersionResource{
        QString::fromUtf8(familyGroup(family)).toUtf8().constData(),
        QString::fromUtf8(familyVersion(family)).toUtf8().constData(),
        QString::fromUtf8(familyResource(family)).toUtf8().constData()};
    endResetModel();
    model_->setScope(std::move(scope));
}

QVariant QtListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    const std::span<const RowRole> columns = familyColumns(family_);
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 ||
        section >= static_cast<int>(columns.size())) {
        return {};
    }
    // The header is the only translatable part of the table; a cell is data.
    return tr(rowRoleLabel(columns[static_cast<std::size_t>(section)]).data());
}

void QtListModel::dataChanged(std::uint32_t first, std::uint32_t last,
                              std::span<const RowRole> roles)
{
    QList<int> translated;
    translated.reserve(static_cast<qsizetype>(roles.size()));
    for (const RowRole role : roles) {
        translated.push_back(static_cast<int>(roleIndex(role)));
    }
    // The sink method shadows the signal of the same name, so the base is named.
    QAbstractListModel::dataChanged(index(static_cast<int>(first)), index(static_cast<int>(last)),
                                   translated);
}

void QtListModel::wake()
{
    // The analyzer cannot see that Qt takes ownership of the functor it
    // allocates here, so the pair is reported as a leak.
    QMetaObject::invokeMethod(this, [this] { model_->drain(); },
                              Qt::QueuedConnection); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
}

} // namespace antares::adapters::ui

#endif // ANTARES_HAS_QT
