#include "protocol/FileTransferCodec.h"

#include <QCoreApplication>
#include "core/ErrorTypes.h"

#include <QRandomGenerator>

#include <cmath>
#include <cstring>

namespace ultraview::protocol {
namespace {

constexpr char kDelimiter[] = "|||";
constexpr qsizetype kDelimiterLength = 3;

bool commandOf(const QList<QByteArray> &fields, int *command)
{
    if (fields.isEmpty() || fields.first().isEmpty())
        return false;
    const QByteArray &first = fields.first();
    if (first.size() > 2)
        return false;
    int value = 0;
    for (int index = static_cast<int>(first.size()) - 1; index >= 0; --index)
        value = (value << 8) | static_cast<quint8>(first.at(index));
    *command = value;
    return true;
}

bool isTransferKind(int command)
{
    return (command >= 13 && command <= 16) || command == 21;
}

QByteArray asciiNumber(qint64 value)
{
    return QByteArray::number(value);
}

/// The delimited ASCII form: `13|||id|||name|||size`.
QByteArray textCommand(int command, const QList<QByteArray> &fields)
{
    QByteArray payload = QByteArray::number(command);
    for (const QByteArray &field : fields) {
        payload.append(kDelimiter, kDelimiterLength);
        payload.append(field);
    }
    return payload;
}

FileTransferMessage malformed(const QString &message, const QString &detail = {})
{
    throwProtocol(message, detail);
}

} // namespace

QString FileTransferCodec::newTransferId()
{
    // 10 decimal digits, unique enough per session; the host treats it as an
    // opaque ASCII token and echoes it back unchanged.
    const quint32 value = QRandomGenerator::global()->bounded(quint32(1000000000U));
    return QStringLiteral("%1").arg(1000000000U + value);
}

bool FileTransferCodec::isSafeFileName(const QString &name)
{
    if (name.isEmpty() || name.size() > 255)
        return false;
    if (name == QLatin1String(".") || name == QLatin1String(".."))
        return false;
    if (name.contains(QLatin1String(kDelimiter)))
        return false;
    for (const QChar character : name) {
        if (character.category() == QChar::Other_Control)
            return false;
        // Both separators are refused regardless of the receiving platform.
        if (character == QLatin1Char('/') || character == QLatin1Char('\\'))
            return false;
    }
    return true;
}

QByteArray FileTransferCodec::offer(const QString &transferId, const QString &name, qint64 size)
{
    if (!isSafeFileName(name)) {
        throwError(ErrorCode::Configuration, QCoreApplication::translate("FileTransferCodec", "the file name cannot be sent"),
                   QCoreApplication::translate("FileTransferCodec", "only plain file names are accepted"));
    }
    if (size < 0 || size > kMaxFileBytes) {
        throwError(ErrorCode::Configuration, QCoreApplication::translate("FileTransferCodec", "the file size cannot be sent"));
    }
    return textCommand(13, {transferId.toLatin1(), name.toLatin1(), asciiNumber(size)});
}

QByteArray FileTransferCodec::accept(const QString &transferId, bool accepted)
{
    return textCommand(14, {transferId.toLatin1(), accepted ? QByteArrayLiteral("1")
                                                           : QByteArrayLiteral("0")});
}

QByteArray FileTransferCodec::chunk(const QString &transferId, qint64 offset,
                                    const QByteArray &data)
{
    if (offset < 0 || offset > kMaxFileBytes)
        throwError(ErrorCode::Configuration, QCoreApplication::translate("FileTransferCodec", "invalid file transfer offset"));
    if (data.isEmpty() || data.size() > kMaxChunkBytes)
        throwError(ErrorCode::Configuration, QCoreApplication::translate("FileTransferCodec", "invalid file transfer chunk length"),
                   QStringLiteral("%1 bytes").arg(data.size()));
    QByteArray payload = textCommand(15, {transferId.toLatin1(), asciiNumber(offset)});
    payload.append(kDelimiter, kDelimiterLength);
    payload.append(data);
    return payload;
}

QByteArray FileTransferCodec::cancel(const QString &transferId)
{
    return textCommand(16, {transferId.toLatin1()});
}

QByteArray FileTransferCodec::acknowledge(const QString &transferId)
{
    return textCommand(21, {transferId.toLatin1()});
}

QByteArray FileTransferCodec::encodeDouble(qint64 value)
{
    const double numeric = static_cast<double>(value);
    quint64 bits = 0;
    static_assert(sizeof(bits) == sizeof(numeric));
    std::memcpy(&bits, &numeric, sizeof(bits));
    QByteArray bytes(8, Qt::Uninitialized);
    for (int index = 0; index < 8; ++index)
        bytes[index] = static_cast<char>((bits >> (8 * index)) & 0xffU);
    return bytes;
}

bool FileTransferCodec::decodeDouble(const QByteArray &bytes, qint64 *value)
{
    if (bytes.size() != 8 || value == nullptr)
        return false;
    quint64 bits = 0;
    for (int index = 7; index >= 0; --index)
        bits = (bits << 8) | static_cast<quint8>(bytes.at(index));
    double numeric = 0.0;
    std::memcpy(&numeric, &bits, sizeof(numeric));
    if (!std::isfinite(numeric) || numeric < 0.0
        || numeric > static_cast<double>(kMaxFileBytes)) {
        return false;
    }
    *value = static_cast<qint64>(numeric);
    return true;
}

bool FileTransferCodec::isTransferCommand(const QList<QByteArray> &fields)
{
    int command = 0;
    return commandOf(fields, &command) && isTransferKind(command);
}

FileTransferMessage FileTransferCodec::parse(const QList<QByteArray> &fields)
{
    FileTransferMessage message;
    int command = 0;
    if (!commandOf(fields, &command))
        return message;
    if (command == 13) {
        if (fields.size() < 4)
            malformed(QCoreApplication::translate("FileTransferCodec", "file offer is incomplete"));
        message.kind = FileTransferMessage::Kind::Offer;
        message.offer.transferId = QString::fromLatin1(fields.at(1));
        message.offer.name = QString::fromLatin1(fields.at(2));
        if (!decodeDouble(fields.at(3), &message.offer.size))
            malformed(QCoreApplication::translate("FileTransferCodec", "file offer carries an invalid size"));
        if (!isSafeFileName(message.offer.name))
            malformed(QCoreApplication::translate("FileTransferCodec", "file offer carries an unusable file name"));
        return message;
    }
    if (command == 14) {
        if (fields.size() < 2)
            malformed(QCoreApplication::translate("FileTransferCodec", "file acceptance is incomplete"));
        message.kind = FileTransferMessage::Kind::Accept;
        message.transferId = QString::fromLatin1(fields.at(1));
        message.accepted = fields.size() > 2 && !fields.at(2).isEmpty()
            && fields.at(2).at(0) != 0;
        return message;
    }
    if (command == 15) {
        if (fields.size() < 4)
            malformed(QCoreApplication::translate("FileTransferCodec", "file chunk is incomplete"));
        message.kind = FileTransferMessage::Kind::Chunk;
        message.chunk.transferId = QString::fromLatin1(fields.at(1));
        if (!decodeDouble(fields.at(2), &message.chunk.offset))
            malformed(QCoreApplication::translate("FileTransferCodec", "file chunk carries an invalid offset"));
        message.chunk.data = fields.at(3);
        if (message.chunk.data.isEmpty())
            malformed(QCoreApplication::translate("FileTransferCodec", "file chunk carries no data"));
        if (message.chunk.data.size() > kMaxInboundChunkBytes)
            malformed(QCoreApplication::translate("FileTransferCodec", "file chunk is larger than the protocol allows"),
                      QStringLiteral("%1 bytes").arg(message.chunk.data.size()));
        return message;
    }
    if (command == 16) {
        if (fields.size() < 2)
            malformed(QCoreApplication::translate("FileTransferCodec", "file cancellation carries no transfer id"));
        message.kind = FileTransferMessage::Kind::Cancel;
        message.transferId = QString::fromLatin1(fields.at(1));
        return message;
    }
    if (command == 21) {
        if (fields.size() < 2)
            malformed(QCoreApplication::translate("FileTransferCodec", "chunk acknowledgement carries no transfer id"));
        message.kind = FileTransferMessage::Kind::Ack;
        message.transferId = QString::fromLatin1(fields.at(1));
        return message;
    }
    return message;
}

FileTransferMessage FileTransferCodec::parseText(const QByteArray &payload)
{
    FileTransferMessage message;
    if (payload.isEmpty())
        return message;
    // The ASCII form cannot carry raw chunk data, so only the control messages
    // are accepted here; a chunk always arrives multipart.
    const qsizetype space = payload.indexOf(kDelimiter, 0);
    if (space <= 0)
        return message;
    bool ok = false;
    const int command = static_cast<int>(payload.left(space).toInt(&ok));
    if (!ok || !isTransferKind(command))
        return message;
    // `QByteArray::split` takes a single character, so the three-character
    // delimiter needs an explicit walk.
    QList<QByteArray> fields;
    qsizetype start = space + kDelimiterLength;
    while (true) {
        const qsizetype next = payload.indexOf(kDelimiter, start);
        if (next < 0) {
            fields.append(payload.mid(start));
            break;
        }
        fields.append(payload.mid(start, next - start));
        start = next + kDelimiterLength;
    }

    if (command == 13) {
        if (fields.size() < 3)
            malformed(QCoreApplication::translate("FileTransferCodec", "text file offer is incomplete"));
        message.kind = FileTransferMessage::Kind::Offer;
        message.offer.transferId = QString::fromLatin1(fields.at(0));
        message.offer.name = QString::fromLatin1(fields.at(1));
        bool sizeOk = false;
        message.offer.size = fields.at(2).toLongLong(&sizeOk);
        if (!sizeOk || message.offer.size < 0 || message.offer.size > kMaxFileBytes)
            malformed(QCoreApplication::translate("FileTransferCodec", "text file offer carries an invalid size"));
        if (!isSafeFileName(message.offer.name))
            malformed(QCoreApplication::translate("FileTransferCodec", "text file offer carries an unusable file name"));
        return message;
    }
    if (!fields.isEmpty())
        message.transferId = QString::fromLatin1(fields.first());
    if (command == 14) {
        message.kind = FileTransferMessage::Kind::Accept;
        message.accepted = fields.size() > 1 && fields.at(1) != QByteArrayLiteral("0");
        return message;
    }
    if (command == 16) {
        message.kind = FileTransferMessage::Kind::Cancel;
        return message;
    }
    if (command == 21) {
        message.kind = FileTransferMessage::Kind::Ack;
        return message;
    }
    return message;
}

} // namespace ultraview::protocol
