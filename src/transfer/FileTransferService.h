#pragma once
//
// File transfer state machine (commands 13, 14, 15, 16 and 21).
//
// The service lives in the session worker thread: it owns the open files, paces
// uploads with the one-chunk acknowledgement window and turns inbound chunks into
// disk writes.  It never touches the network itself -- the session forwards
// `outbound()` payloads to the relay and feeds inbound messages in -- which keeps
// the whole state machine testable without TLS.
//
// Safety properties:
//   * incoming offers are only accepted explicitly (or with auto-accept enabled)
//     and the file is created inside `incomingDirectory()` with a sanitized,
//     non-overwriting name,
//   * a chunk whose offset does not continue the stream, an oversized file or a
//     failed write cancels the transfer instead of writing garbage,
//   * at most `kMaxActiveTransfers` transfers exist at once,
//   * both sides are always acknowledged or cancelled, so a partner never waits
//     for a window that will not advance.
//
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>
#include <vector>

QT_BEGIN_NAMESPACE
class QFile;
QT_END_NAMESPACE

namespace ultraview::transfer {

class FileTransferService : public QObject {
    Q_OBJECT

public:
    enum class Direction { Upload, Download };
    enum class State { Waiting, Transferring, Completed, Cancelled, Failed };

    static constexpr int kMaxActiveTransfers = 8;

    explicit FileTransferService(QObject *parent = nullptr);

    /// `<Downloads>/Ultraview`, with the home directory as the fallback.  The UI
    /// shows this path so a received file can always be found.
    static QString defaultIncomingDirectory();
    ~FileTransferService() override;

    /// Directory received files are written to.  Created on demand.
    void setIncomingDirectory(const QString &directory);
    QString incomingDirectory() const { return m_incomingDirectory; }

    /// Accept every offer without asking the user.  Off by default.
    void setAutoAccept(bool enabled) { m_autoAccept = enabled; }
    bool autoAccept() const { return m_autoAccept; }

    /// Start an upload.  Returns the transfer id, or an empty string with
    /// `error` filled in.
    QString offerFile(const QString &localPath, QString *error);

    /// Answer an incoming offer (no-op for an unknown or already answered id).
    void acceptIncoming(const QString &transferId, bool accept);

    /// Cancel one transfer, or every transfer.
    void cancel(const QString &transferId);
    void cancelAll();

    /// Feed an inbound message (multipart or delimited text).
    void handleInbound(const QList<QByteArray> &fields);
    void handleInboundText(const QByteArray &payload);

    /// Rows for the UI: id, name, size, transferred, progress, direction,
    /// state, path, error.
    QVariantList rows() const;
    int activeCount() const;
    bool hasPendingOffers() const;

signals:
    /// A payload for the relay (relay flag 0).
    void outbound(const QByteArray &payload);
    void transfersChanged(const QVariantList &rows);
    /// User-visible, credential-free message.
    void notice(const QString &message);

private:
    struct Transfer {
        QString id;
        QString name;
        QString path;
        qint64 size = 0;
        qint64 transferred = 0;
        Direction direction = Direction::Download;
        State state = State::Waiting;
        QString error;
        QFile *file = nullptr;
    };

    Transfer *find(const QString &transferId);
    const Transfer *find(const QString &transferId) const;
    void startOrContinue(Transfer *transfer);
    void finish(Transfer *transfer, State state, const QString &error = {});
    void closeFile(Transfer *transfer);
    void report();
    QString uniqueIncomingPath(const QString &name) const;
    bool ensureIncomingDirectory();
    int indexOf(const QString &transferId) const;

    std::vector<std::unique_ptr<Transfer>> m_transfers;
    QString m_incomingDirectory;
    bool m_autoAccept = false;
};

} // namespace ultraview::transfer
