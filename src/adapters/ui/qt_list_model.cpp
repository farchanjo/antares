#include "adapters/ui/qt_list_model.hpp"

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
    return static_cast<int>(model_->rowCount());
}

QVariant QtListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    // A view may ask by role (the registered 257..260) or, as a table view does
    // for the default role, by column. Both resolve to the same field.
    std::optional<RowRole> wanted = roleForIndex(static_cast<std::uint32_t>(role));
    if (!wanted && index.column() >= 0 && index.column() < static_cast<int>(ROW_ROLE_COUNT)) {
        wanted = static_cast<RowRole>(index.column());
    }
    if (!wanted) {
        return {};
    }
    const std::string text = model_->data(static_cast<std::uint32_t>(index.row()), *wanted)
                                 .value_or(std::string{});
    return QVariant{toQt(text)};
}

void QtListModel::multiData(const QModelIndex &index, QModelRoleDataSpan span) const
{
    if (!index.isValid()) {
        return;
    }
    std::array<RowRole, ROW_ROLE_COUNT> roles{};
    std::array<std::uint32_t, ROW_ROLE_COUNT> positions{};
    std::size_t count = 0;
    for (const QModelRoleData &entry : span) {
        std::optional<RowRole> role = roleForIndex(static_cast<std::uint32_t>(entry.role()));
        // Same rule as `data`: a column stands in for an unregistered role.
        if (!role && index.column() >= 0 && index.column() < static_cast<int>(ROW_ROLE_COUNT)) {
            role = static_cast<RowRole>(index.column());
        }
        if (!role || count >= ROW_ROLE_COUNT) {
            continue;
        }
        roles[count] = *role;
        positions[count] = static_cast<std::uint32_t>(count);
        ++count;
    }

    std::array<std::string, ROW_ROLE_COUNT> texts{};
    model_->multiData(static_cast<std::uint32_t>(index.row()),
                      std::span<const RowRole>{roles.data(), count},
                      std::span<std::string>{texts.data(), count});
    for (std::size_t at = 0; at < count; ++at) {
        span.data()[positions[at]].setData(QVariant{toQt(texts[at])});
    }
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
    return parent.isValid() ? 0 : static_cast<int>(ROW_ROLE_COUNT);
}

QVariant QtListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 ||
        section >= static_cast<int>(ROW_ROLE_COUNT)) {
        return {};
    }
    // The header is the only translatable part of the table; a cell is data.
    return tr(rowRoleName(static_cast<RowRole>(section)).data());
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
