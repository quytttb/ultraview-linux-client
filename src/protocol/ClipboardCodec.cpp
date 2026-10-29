#include "protocol/ClipboardCodec.h"

#include <QCoreApplication>
#include "core/ErrorTypes.h"
#include "protocol/Digest.h"
#include "protocol/ZstdCodec.h"

#include <QStringDecoder>

#include <cstring>

namespace ultraview::protocol {
namespace {

constexpr char kDelimiter[] = "|||";
constexpr qsizetype kDelimiterLength = 3;

/// Reads a little-endian unsigned integer of `size` bytes (4 or 8 only).
bool readLittleEndian(const QByteArray &bytes, quint64 *value)
{
    if (bytes.size() != 4 && bytes.size() != 8)
        return false;
    quint64 result = 0;
    for (int index = static_cast<int>(bytes.size()) - 1; index >= 0; --index) {
        result = (result << 8) | static_cast<quint8>(bytes.at(index));
    }
    *value = result;
    return true;
}

} // namespace

QByteArray ClipboardCodec::toUtf16(const QString &text)
{
    return utf16leBytes(text);
}

std::optional<QString> ClipboardCodec::fromUtf16(const QByteArray &bytes)
{
    if (bytes.size() % 2 != 0)
        return std::nullopt;
    // `QString::fromUtf16` takes UTF-16 code units, so the byte count is halved;
    // an unpaired surrogate is preserved rather than replaced (the text is then
    // shown as-is instead of being silently corrupted).
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData()),
                              bytes.size() / 2);
}

QByteArray ClipboardCodec::modernHeader(qint64 utf16ByteCount)
{
    QByteArray header;
    header.append(QByteArray::number(static_cast<int>(kModernCommand)));
    header.append(kDelimiter, kDelimiterLength);
    header.append(QByteArray::number(utf16ByteCount));
    header.append(kDelimiter, kDelimiterLength);
    return header;
}

ClipboardCodec::Upload ClipboardCodec::buildLegacy(const QString &text)
{
    if (text.contains(QLatin1String(kDelimiter))) {
        throwError(ErrorCode::UnsupportedOperation,
                   QCoreApplication::translate("ClipboardCodec", "clipboard text contains the protocol delimiter"),
                   QCoreApplication::translate("ClipboardCodec", "the legacy clipboard encoding cannot carry \"|||\""));
    }
    Upload upload;
    upload.payload = toUtf16(QString::fromLatin1("5") + QLatin1String(kDelimiter) + text);
    upload.flag = 1;
    return upload;
}

ClipboardCodec::Upload ClipboardCodec::buildModern(const QString &text)
{
    if (text.isEmpty())
        throwError(ErrorCode::UnsupportedOperation,
                   QCoreApplication::translate("ClipboardCodec", "the modern clipboard encoding cannot carry empty text"));
    const QByteArray utf16 = toUtf16(text);
    if (utf16.size() > kMaxClipboardBytes) {
        throwError(ErrorCode::Resource,
                   QCoreApplication::translate("ClipboardCodec", "clipboard text is too large to send"),
                   QStringLiteral("%1 bytes, limit %2").arg(utf16.size()).arg(kMaxClipboardBytes));
    }
    const QByteArray compressed = ZstdCodec::compress(utf16);
    if (compressed.isEmpty())
        throwError(ErrorCode::Internal, QCoreApplication::translate("ClipboardCodec", "clipboard text could not be compressed"));

    Upload upload;
    upload.payload = modernHeader(utf16.size()) + compressed;
    upload.flag = 0;
    return upload;
}

ClipboardCodec::Upload ClipboardCodec::buildUpload(const QString &text)
{
    // Only the legacy form is delimited.  The modern form puts the text inside a
    // length-prefixed Zstandard frame, so `|||` in the text is harmless there and
    // must not force the lossy legacy path.
    if (text.isEmpty())
        return buildLegacy(text);
    return buildModern(text);
}

std::optional<QString> ClipboardCodec::parseLegacy(const QByteArray &payload)
{
    if (payload.size() % 2 != 0)
        return std::nullopt;
    const std::optional<QString> text = fromUtf16(payload);
    if (!text.has_value())
        return std::nullopt;

    const QString prefix = QString::fromLatin1("5") + QLatin1String(kDelimiter);
    if (text->startsWith(prefix))
        return text->mid(prefix.size());
    // A host may send the bare text with the UTF-16 flag set; accept it, because
    // there is no other message that arrives as flag-1 text with no prefix.
    return *text;
}

bool ClipboardCodec::isModernCommand(const QList<QByteArray> &fields)
{
    return !fields.isEmpty() && !fields.first().isEmpty()
        && static_cast<quint8>(fields.first().at(0)) == kModernCommand;
}

std::optional<QString> ClipboardCodec::parseModern(const QList<QByteArray> &fields)
{
    if (!isModernCommand(fields))
        return std::nullopt;
    if (fields.size() < 3)
        throwProtocol(QCoreApplication::translate("ClipboardCodec", "clipboard message carries no text"),
                      QStringLiteral("%1 fields").arg(fields.size()));

    quint64 declared = 0;
    if (!readLittleEndian(fields.at(1), &declared))
        throwProtocol(QCoreApplication::translate("ClipboardCodec", "clipboard message has a malformed length field"));
    const QByteArray utf16 = ZstdCodec::decompress(fields.at(2), static_cast<qint64>(declared),
                                                  kMaxClipboardBytes);
    const std::optional<QString> text = fromUtf16(utf16);
    if (!text.has_value())
        throwProtocol(QCoreApplication::translate("ClipboardCodec", "clipboard message does not contain UTF-16 text"));
    return *text;
}

} // namespace ultraview::protocol
