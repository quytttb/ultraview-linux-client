#pragma once
//
// Recently used partner ids.
//
// Only the partner id and the time of the last attempt are stored: the reference
// keeps the whole partner profile on disk, including the password, which this
// client deliberately does not do (see security/CredentialStore).
//
#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QString>

namespace ultraview::models {

class RecentConnectionsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        PartnerIdRole = Qt::UserRole + 1,
        LastUsedRole,
        LastUsedTextRole,
    };

    explicit RecentConnectionsModel(QObject *parent = nullptr);
    ~RecentConnectionsModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Record an attempt for `partnerId` (moves it to the front).
    Q_INVOKABLE void record(const QString &partnerId);
    Q_INVOKABLE void removeAt(int row);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QString partnerIdAt(int row) const;

    /// Maximum number of entries kept.
    static constexpr int kLimit = 20;

signals:
    void countChanged();

private:
    void load();
    void persist();

    struct Entry {
        QString partnerId;
        QDateTime lastUsed;
    };
    QList<Entry> m_entries;
};

} // namespace ultraview::models

Q_DECLARE_METATYPE(ultraview::models::RecentConnectionsModel *)
