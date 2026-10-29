#include "transfer/FileTransferService.h"

#include "core/ErrorTypes.h"
#include "protocol/FileTransferCodec.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QStandardPaths>

#include <algorithm>

namespace ultraview::transfer {
namespace {

QString stateName(FileTransferService::State state)
{
    switch (state) {
    case FileTransferService::State::Waiting:
        return QStringLiteral("waiting");
    case FileTransferService::State::Transferring:
        return QStringLiteral("transferring");
    case FileTransferService::State::Completed:
        return QStringLiteral("completed");
    case FileTransferService::State::Cancelled:
        return QStringLiteral("cancelled");
    case FileTransferService::State::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("failed");
}

QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

} // namespace

QString FileTransferService::defaultIncomingDirectory()
{
    QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (directory.isEmpty())
        directory = QDir::homePath();
    return directory + QStringLiteral("/Ultraview");
}

FileTransferService::FileTransferService(QObject *parent)
    : QObject(parent)
{
    m_incomingDirectory = defaultIncomingDirectory();
}

FileTransferService::~FileTransferService()
{
    // No signals from a destructor: just drop the handles and the partial files.
    for (const auto &transfer : m_transfers) {
        if (transfer->file != nullptr) {
            transfer->file->close();
            delete transfer->file;
            transfer->file = nullptr;
        }
        if (transfer->direction == Direction::Download && transfer->state != State::Completed
            && !transfer->path.isEmpty()) {
            QFile::remove(transfer->path);
        }
    }
    m_transfers.clear();
}

void FileTransferService::setIncomingDirectory(const QString &directory)
{
    m_incomingDirectory = directory;
}

int FileTransferService::indexOf(const QString &transferId) const
{
    for (size_t index = 0; index < m_transfers.size(); ++index) {
        if (m_transfers[index]->id == transferId)
            return static_cast<int>(index);
    }
    return -1;
}

FileTransferService::Transfer *FileTransferService::find(const QString &transferId)
{
    const int index = indexOf(transferId);
    return index < 0 ? nullptr : m_transfers[static_cast<size_t>(index)].get();
}

const FileTransferService::Transfer *FileTransferService::find(const QString &transferId) const
{
    const int index = indexOf(transferId);
    return index < 0 ? nullptr : m_transfers[static_cast<size_t>(index)].get();
}

QString FileTransferService::uniqueIncomingPath(const QString &name) const
{
    const QDir directory(m_incomingDirectory);
    QString candidate = directory.filePath(name);
    if (!QFileInfo::exists(candidate))
        return candidate;
    const QFileInfo info(name);
    const QString suffix = info.suffix();
    const QString base = info.completeBaseName();
    for (int counter = 1; counter < 10000; ++counter) {
        const QString numbered = suffix.isEmpty()
            ? QStringLiteral("%1 (%2)").arg(base).arg(counter)
            : QStringLiteral("%1 (%2).%3").arg(base).arg(counter).arg(suffix);
        candidate = directory.filePath(numbered);
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
    return QString();
}

bool FileTransferService::ensureIncomingDirectory()
{
    QDir directory(m_incomingDirectory);
    return directory.exists() || directory.mkpath(QStringLiteral("."));
}

void FileTransferService::report()
{
    emit transfersChanged(rows());
}

QVariantList FileTransferService::rows() const
{
    QVariantList list;
    list.reserve(static_cast<int>(m_transfers.size()));
    for (const auto &transfer : m_transfers) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), transfer->id);
        row.insert(QStringLiteral("name"), transfer->name);
        row.insert(QStringLiteral("size"), transfer->size);
        row.insert(QStringLiteral("sizeText"), sizeText(transfer->size));
        row.insert(QStringLiteral("transferred"), transfer->transferred);
        row.insert(QStringLiteral("transferredText"), sizeText(transfer->transferred));
        row.insert(QStringLiteral("progress"),
                   transfer->size > 0
                       ? std::clamp(static_cast<double>(transfer->transferred)
                                        / static_cast<double>(transfer->size),
                                    0.0, 1.0)
                       : (transfer->state == State::Completed ? 1.0 : 0.0));
        row.insert(QStringLiteral("direction"),
                   transfer->direction == Direction::Upload ? QStringLiteral("upload")
                                                            : QStringLiteral("download"));
        row.insert(QStringLiteral("state"), stateName(transfer->state));
        row.insert(QStringLiteral("path"), transfer->path);
        row.insert(QStringLiteral("error"), transfer->error);
        list.append(row);
    }
    return list;
}

int FileTransferService::activeCount() const
{
    int count = 0;
    for (const auto &transfer : m_transfers) {
        if (transfer->state == State::Waiting || transfer->state == State::Transferring)
            ++count;
    }
    return count;
}

bool FileTransferService::hasPendingOffers() const
{
    for (const auto &transfer : m_transfers) {
        if (transfer->state == State::Waiting && transfer->direction == Direction::Download)
            return true;
    }
    return false;
}

void FileTransferService::closeFile(Transfer *transfer)
{
    if (transfer->file != nullptr) {
        transfer->file->flush();
        transfer->file->close();
        delete transfer->file;
        transfer->file = nullptr;
    }
}

void FileTransferService::finish(Transfer *transfer, State state, const QString &error)
{
    if (transfer->state == State::Completed || transfer->state == State::Cancelled
        || transfer->state == State::Failed) {
        return;
    }
    transfer->state = state;
    transfer->error = error;
    closeFile(transfer);
    if (state != State::Completed && transfer->direction == Direction::Download
        && !transfer->path.isEmpty()) {
        // A partial download is not a file the user asked for; remove it so a
        // truncated transfer cannot be mistaken for a complete one.
        QFile::remove(transfer->path);
    }
    switch (state) {
    case State::Completed:
        emit notice(transfer->direction == Direction::Download
                        ? tr("received \"%1\"").arg(transfer->name)
                        : tr("sent \"%1\"").arg(transfer->name));
        break;
    case State::Failed:
        emit notice(tr("file transfer of \"%1\" failed: %2")
                        .arg(transfer->name, error));
        break;
    case State::Cancelled:
        emit notice(tr("file transfer of \"%1\" was cancelled%2")
                        .arg(transfer->name,
                             error.isEmpty() ? QString() : QStringLiteral(": ") + error));
        break;
    case State::Waiting:
    case State::Transferring:
        break;
    }
    report();
}

QString FileTransferService::offerFile(const QString &localPath, QString *error)
{
    const auto reject = [error](const QString &message) {
        if (error != nullptr)
            *error = message;
        return QString();
    };

    const QFileInfo info(localPath);
    if (!info.exists() || !info.isFile())
        return reject(tr("the file does not exist"));
    if (!info.isReadable())
        return reject(tr("the file is not readable"));
    if (activeCount() >= kMaxActiveTransfers)
        return reject(tr("too many transfers are already active"));
    if (info.size() < 0 || info.size() > protocol::FileTransferCodec::kMaxFileBytes)
        return reject(tr("the file is too large for the protocol"));
    const QString name = info.fileName();
    if (!protocol::FileTransferCodec::isSafeFileName(name))
        return reject(tr("the file name cannot be sent"));
    auto transfer = std::make_unique<Transfer>();
    transfer->id = protocol::FileTransferCodec::newTransferId();
    transfer->name = name;
    transfer->path = info.absoluteFilePath();
    transfer->size = info.size();
    transfer->direction = Direction::Upload;
    transfer->state = State::Waiting;
    auto *file = new QFile(transfer->path);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        return reject(tr("the file could not be opened for reading"));
    }
    transfer->file = file;

    const QString id = transfer->id;
    try {
        const QByteArray payload = protocol::FileTransferCodec::offer(id, name, transfer->size);
        m_transfers.push_back(std::move(transfer));
        emit outbound(payload);
    } catch (const UvException &exception) {
        file->close();
        delete file;
        return reject(exception.message());
    }
    emit notice(tr("offering \"%1\" (%2)").arg(name, sizeText(info.size())));
    report();
    return id;
}

void FileTransferService::acceptIncoming(const QString &transferId, bool accept)
{
    Transfer *transfer = find(transferId);
    if (transfer == nullptr || transfer->direction != Direction::Download
        || transfer->state != State::Waiting) {
        return;
    }
    if (!accept) {
        emit outbound(protocol::FileTransferCodec::accept(transferId, false));
        finish(transfer, State::Cancelled, tr("declined"));
        return;
    }
    if (!ensureIncomingDirectory()) {
        emit outbound(protocol::FileTransferCodec::accept(transferId, false));
        finish(transfer, State::Failed, tr("the download directory is not writable"));
        return;
    }
    const QString path = uniqueIncomingPath(transfer->name);
    if (path.isEmpty()) {
        emit outbound(protocol::FileTransferCodec::accept(transferId, false));
        finish(transfer, State::Failed, tr("no free file name in the download directory"));
        return;
    }
    auto *file = new QFile(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        delete file;
        emit outbound(protocol::FileTransferCodec::accept(transferId, false));
        finish(transfer, State::Failed, tr("the destination file could not be created"));
        return;
    }
    transfer->file = file;
    transfer->path = path;
    transfer->state = State::Transferring;
    emit outbound(protocol::FileTransferCodec::accept(transferId, true));
    if (transfer->size == 0) {
        finish(transfer, State::Completed);
        return;
    }
    report();
}

void FileTransferService::startOrContinue(Transfer *transfer)
{
    if (transfer->state != State::Transferring || transfer->file == nullptr)
        return;
    if (transfer->transferred >= transfer->size) {
        finish(transfer, State::Completed);
        return;
    }
    const qint64 remaining = transfer->size - transfer->transferred;
    const int wanted = static_cast<int>(
        std::min<qint64>(remaining, protocol::FileTransferCodec::kMaxChunkBytes));
    const QByteArray data = transfer->file->read(wanted);
    if (data.isEmpty()) {
        emit outbound(protocol::FileTransferCodec::cancel(transfer->id));
        finish(transfer, State::Failed, tr("the file ended before its announced size"));
        return;
    }
    emit outbound(protocol::FileTransferCodec::chunk(transfer->id, transfer->transferred, data));
    transfer->transferred += data.size();
    report();
}

void FileTransferService::cancel(const QString &transferId)
{
    Transfer *transfer = find(transferId);
    if (transfer == nullptr)
        return;
    if (transfer->state == State::Waiting || transfer->state == State::Transferring)
        emit outbound(protocol::FileTransferCodec::cancel(transferId));
    finish(transfer, State::Cancelled, tr("cancelled locally"));
}

void FileTransferService::cancelAll()
{
    for (const auto &transfer : m_transfers) {
        if (transfer->state == State::Waiting || transfer->state == State::Transferring)
            emit outbound(protocol::FileTransferCodec::cancel(transfer->id));
        finish(transfer.get(), State::Cancelled, tr("session ended"));
    }
}

void FileTransferService::handleInbound(const QList<QByteArray> &fields)
{
    using protocol::FileTransferCodec;
    using protocol::FileTransferMessage;

    const FileTransferMessage message = FileTransferCodec::parse(fields);
    if (message.kind == FileTransferMessage::Kind::None)
        return;

    switch (message.kind) {
    case FileTransferMessage::Kind::Offer: {
        if (find(message.offer.transferId) != nullptr) {
            // A duplicated offer must not create a second file for the same id.
            emit outbound(FileTransferCodec::cancel(message.offer.transferId));
            return;
        }
        if (activeCount() >= kMaxActiveTransfers) {
            emit outbound(FileTransferCodec::cancel(message.offer.transferId));
            emit notice(tr("refused \"%1\": too many active transfers")
                            .arg(message.offer.name));
            return;
        }
        auto transfer = std::make_unique<Transfer>();
        transfer->id = message.offer.transferId;
        transfer->name = message.offer.name;
        transfer->size = message.offer.size;
        transfer->direction = Direction::Download;
        transfer->state = State::Waiting;
        const QString name = transfer->name;
        const qint64 size = transfer->size;
        m_transfers.push_back(std::move(transfer));
        if (m_autoAccept) {
            acceptIncoming(message.offer.transferId, true);
        } else {
            emit notice(tr("the partner offers \"%1\" (%2); accept or decline it "
                                       "in the transfer panel")
                            .arg(name, sizeText(size)));
            report();
        }
        return;
    }
    case FileTransferMessage::Kind::Accept: {
        Transfer *transfer = find(message.transferId);
        if (transfer == nullptr || transfer->direction != Direction::Upload)
            return;
        if (!message.accepted) {
            finish(transfer, State::Cancelled, tr("the partner declined"));
            return;
        }
        if (transfer->state != State::Waiting)
            return;
        transfer->state = State::Transferring;
        startOrContinue(transfer);
        return;
    }
    case FileTransferMessage::Kind::Chunk: {
        Transfer *transfer = find(message.chunk.transferId);
        if (transfer == nullptr || transfer->direction != Direction::Download) {
            emit outbound(FileTransferCodec::cancel(message.chunk.transferId));
            return;
        }
        if (transfer->state != State::Transferring || transfer->file == nullptr) {
            emit outbound(FileTransferCodec::cancel(message.chunk.transferId));
            return;
        }
        if (message.chunk.offset != transfer->transferred) {
            emit outbound(FileTransferCodec::cancel(message.chunk.transferId));
            finish(transfer, State::Failed,
                   tr("chunk at offset %1 arrived while %2 was expected")
                       .arg(message.chunk.offset)
                       .arg(transfer->transferred));
            return;
        }
        if (transfer->file->write(message.chunk.data) != message.chunk.data.size()) {
            emit outbound(FileTransferCodec::cancel(message.chunk.transferId));
            finish(transfer, State::Failed, tr("the file could not be written"));
            return;
        }
        transfer->transferred += message.chunk.data.size();
        emit outbound(FileTransferCodec::acknowledge(message.chunk.transferId));
        if (transfer->transferred >= transfer->size)
            finish(transfer, State::Completed);
        else
            report();
        return;
    }
    case FileTransferMessage::Kind::Cancel: {
        Transfer *transfer = find(message.transferId);
        if (transfer != nullptr)
            finish(transfer, State::Cancelled, tr("the partner cancelled"));
        return;
    }
    case FileTransferMessage::Kind::Ack: {
        Transfer *transfer = find(message.transferId);
        if (transfer == nullptr || transfer->direction != Direction::Upload
            || transfer->state != State::Transferring) {
            return;
        }
        startOrContinue(transfer);
        return;
    }
    case FileTransferMessage::Kind::None:
        return;
    }
}

void FileTransferService::handleInboundText(const QByteArray &payload)
{
    using protocol::FileTransferCodec;
    using protocol::FileTransferMessage;

    const FileTransferMessage message = FileTransferCodec::parseText(payload);
    if (message.kind == FileTransferMessage::Kind::None)
        return;
    switch (message.kind) {
    case FileTransferMessage::Kind::Offer:
        handleInbound({QByteArray(1, static_cast<char>(13)),
                       message.offer.transferId.toLatin1(),
                       message.offer.name.toLatin1(),
                       FileTransferCodec::encodeDouble(message.offer.size)});
        return;
    case FileTransferMessage::Kind::Accept:
    case FileTransferMessage::Kind::Cancel:
    case FileTransferMessage::Kind::Ack: {
        const int command = message.kind == FileTransferMessage::Kind::Accept  ? 14
            : message.kind == FileTransferMessage::Kind::Cancel               ? 16
                                                                              : 21;
        handleInbound({QByteArray(1, static_cast<char>(command)),
                       message.transferId.toLatin1()});
        return;
    }
    case FileTransferMessage::Kind::Chunk:
        // A chunk never arrives as text; the codec refuses it as well.
        return;
    case FileTransferMessage::Kind::None:
        return;
    }
}

} // namespace ultraview::transfer
