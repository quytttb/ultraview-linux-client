#include "protocol/Digest.h"

#include <QCryptographicHash>

namespace ultraview::protocol {

QByteArray utf16leBytes(const QString &text)
{
    QByteArray out;
    out.resize(text.size() * 2);
    char *cursor = out.data();
    for (const QChar unit : text) {
        const char16_t value = unit.unicode();
        *cursor++ = static_cast<char>(value & 0xFF);
        *cursor++ = static_cast<char>((value >> 8) & 0xFF);
    }
    return out;
}

QByteArray utf16leBytesLimited(const QString &text, int maxCodeUnits)
{
    if (maxCodeUnits <= 0)
        return {};
    const QString limited = text.size() > maxCodeUnits ? text.left(maxCodeUnits) : text;
    return utf16leBytes(limited);
}

QString md5HexLower(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

QString md5HexUpper(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex().toUpper());
}

QByteArray sha1Bytes(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha1);
}

} // namespace ultraview::protocol
