#include "models/RecentConnectionsModel.h"

#include <QSettings>

#include <algorithm>

namespace ultraview::models {
namespace {

QSettings settings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("ultraview"),
                     QStringLiteral("client"));
}

} // namespace

RecentConnectionsModel::RecentConnectionsModel(QObject *parent)
    : QAbstractListModel(parent)
{
    load();
}

RecentConnectionsModel::~RecentConnectionsModel() = default;

int RecentConnectionsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_entries.size());
}

QVariant RecentConnectionsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case PartnerIdRole:
        return entry.partnerId;
    case LastUsedRole:
        return entry.lastUsed;
    case LastUsedTextRole:
        return entry.lastUsed.isValid() ? entry.lastUsed.toString(Qt::ISODate)
                                        : QStringLiteral("never");
    default:
        return {};
    }
}

QHash<int, QByteArray> RecentConnectionsModel::roleNames() const
{
    return {{PartnerIdRole, "partnerId"}, {LastUsedRole, "lastUsed"}, {LastUsedTextRole, "lastUsedText"}};
}

void RecentConnectionsModel::load()
{
    QSettings store = settings();
    const int size = store.beginReadArray(QStringLiteral("recent"));
    m_entries.clear();
    for (int index = 0; index < size; ++index) {
        store.setArrayIndex(index);
        Entry entry;
        entry.partnerId = store.value(QStringLiteral("id")).toString();
        entry.lastUsed = store.value(QStringLiteral("lastUsed")).toDateTime();
        if (!entry.partnerId.isEmpty())
            m_entries.append(entry);
    }
    store.endArray();
}

void RecentConnectionsModel::persist()
{
    QSettings store = settings();
    store.remove(QStringLiteral("recent"));
    store.beginWriteArray(QStringLiteral("recent"), static_cast<int>(m_entries.size()));
    for (int index = 0; index < m_entries.size(); ++index) {
        store.setArrayIndex(index);
        store.setValue(QStringLiteral("id"), m_entries.at(index).partnerId);
        store.setValue(QStringLiteral("lastUsed"), m_entries.at(index).lastUsed);
    }
    store.endArray();
    store.sync();
}

void RecentConnectionsModel::record(const QString &partnerId)
{
    const QString trimmed = partnerId.trimmed();
    if (trimmed.isEmpty())
        return;
    const int existing = [this, &trimmed] {
        for (int index = 0; index < m_entries.size(); ++index) {
            if (m_entries.at(index).partnerId == trimmed)
                return index;
        }
        return -1;
    }();
    if (existing >= 0) {
        if (existing != 0) {
            beginMoveRows(QModelIndex(), existing, existing, QModelIndex(), 0);
            m_entries.move(existing, 0);
            endMoveRows();
        }
    } else {
        beginInsertRows(QModelIndex(), 0, 0);
        m_entries.prepend(Entry{trimmed, QDateTime::currentDateTime()});
        endInsertRows();
        emit countChanged();
    }
    m_entries[0].lastUsed = QDateTime::currentDateTime();
    while (m_entries.size() > kLimit) {
        beginRemoveRows(QModelIndex(), m_entries.size() - 1, m_entries.size() - 1);
        m_entries.removeLast();
        endRemoveRows();
        emit countChanged();
    }
    const QModelIndex top = index(0, 0);
    emit dataChanged(top, top);
    persist();
}

void RecentConnectionsModel::removeAt(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_entries.removeAt(row);
    endRemoveRows();
    emit countChanged();
    persist();
}

void RecentConnectionsModel::clear()
{
    if (m_entries.isEmpty())
        return;
    beginResetModel();
    m_entries.clear();
    endResetModel();
    emit countChanged();
    persist();
}

QString RecentConnectionsModel::partnerIdAt(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return {};
    return m_entries.at(row).partnerId;
}

} // namespace ultraview::models
