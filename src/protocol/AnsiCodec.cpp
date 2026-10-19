#include "protocol/AnsiCodec.h"

#include "core/ErrorTypes.h"

#include <array>
#include <QStringConverter>

namespace ultraview::protocol {
namespace {

// Unicode code points for cp1252 bytes 0x80..0x9F.  0x81, 0x8D, 0x8F, 0x90 and
// 0x9D are undefined in cp1252; every mainstream implementation (Python's
// cp1252, Windows) refuses to decode/encode them, and this client does too.
constexpr char32_t kCp1252High[32] = {
    0x20AC, 0xFFFF, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFF, 0x017D, 0xFFFF,
    0xFFFF, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFF, 0x017E, 0x0178,
};

constexpr char32_t kUndefined = 0xFFFF;

std::optional<char32_t> cp1252ToUnicode(quint8 byte)
{
    if (byte < 0x80)
        return char32_t{byte};
    if (byte < 0xA0) {
        const char32_t value = kCp1252High[byte - 0x80];
        if (value == kUndefined)
            return std::nullopt;
        return value;
    }
    return char32_t{byte};
}

std::optional<quint8> unicodeToCp1252(char32_t code)
{
    if (code < 0x80)
        return static_cast<quint8>(code);
    if (code >= 0xA0 && code <= 0xFF)
        return static_cast<quint8>(code);
    for (int index = 0; index < 32; ++index) {
        if (kCp1252High[index] == code && code != kUndefined)
            return static_cast<quint8>(0x80 + index);
    }
    return std::nullopt;
}

QString bytesToHexPreview(const QByteArray &bytes, int limit = 24)
{
    const QByteArray head = bytes.left(limit);
    QString text = QString::fromLatin1(head.toHex(' '));
    if (bytes.size() > limit)
        text += QStringLiteral(" ...");
    return text;
}

} // namespace

std::optional<AnsiCodec> AnsiCodec::fromName(QStringView name)
{
    const QString normalized = name.toString().trimmed().toLower();
    if (normalized.isEmpty())
        return std::nullopt;
    if (normalized == QLatin1String("cp1252") || normalized == QLatin1String("windows-1252")
        || normalized == QLatin1String("windows1252") || normalized == QLatin1String("cp-1252")
        || normalized == QLatin1String("1252"))
        return cp1252();
    if (normalized == QLatin1String("latin1") || normalized == QLatin1String("latin-1")
        || normalized == QLatin1String("iso-8859-1") || normalized == QLatin1String("iso8859-1")
        || normalized == QLatin1String("cp819"))
        return AnsiCodec{Kind::Latin1};
    if (normalized == QLatin1String("ascii") || normalized == QLatin1String("us-ascii")
        || normalized == QLatin1String("cp20127"))
        return AnsiCodec{Kind::Ascii};
    if (normalized == QLatin1String("utf-8") || normalized == QLatin1String("utf8"))
        return AnsiCodec{Kind::Utf8};
    return std::nullopt;
}

QString AnsiCodec::name() const
{
    switch (m_kind) {
    case Kind::Cp1252: return QStringLiteral("cp1252");
    case Kind::Latin1: return QStringLiteral("latin1");
    case Kind::Ascii: return QStringLiteral("ascii");
    case Kind::Utf8: return QStringLiteral("utf-8");
    }
    return QStringLiteral("unknown");
}

QByteArray AnsiCodec::encode(const QString &text) const
{
    QByteArray out;
    out.reserve(text.size());
    for (const QChar unit : text) {
        const char32_t code = unit.unicode();
        switch (m_kind) {
        case Kind::Utf8:
            break; // handled below
        case Kind::Ascii:
            if (code > 0x7F)
                throwProtocol(QStringLiteral("character is outside ASCII"),
                              QStringLiteral("U+%1").arg(static_cast<uint>(code), 4, 16, QLatin1Char('0')));
            out.append(static_cast<char>(code));
            continue;
        case Kind::Latin1:
            if (code > 0xFF)
                throwProtocol(QStringLiteral("character is outside latin1"),
                              QStringLiteral("U+%1").arg(static_cast<uint>(code), 4, 16, QLatin1Char('0')));
            out.append(static_cast<char>(code));
            continue;
        case Kind::Cp1252: {
            const std::optional<quint8> mapped = unicodeToCp1252(code);
            if (!mapped.has_value())
                throwProtocol(QStringLiteral("character is not representable in cp1252"),
                              QStringLiteral("U+%1").arg(static_cast<uint>(code), 4, 16, QLatin1Char('0')));
            out.append(static_cast<char>(*mapped));
            continue;
        }
        }
        break;
    }
    if (m_kind == Kind::Utf8) {
        out = text.toUtf8();
        // Strictness: refuse conversions that lost information (lone surrogates).
        if (QString::fromUtf8(out) != text)
            throwProtocol(QStringLiteral("text cannot be encoded as strict UTF-8"));
    }
    return out;
}

QString AnsiCodec::decode(const QByteArray &bytes) const
{
    switch (m_kind) {
    case Kind::Utf8: {
        QString text;
        text.reserve(bytes.size());
        for (int index = 0; index < bytes.size();) {
            const quint8 lead = static_cast<quint8>(bytes.at(index));
            int length = 1;
            char32_t code = lead;
            if (lead < 0x80) {
                length = 1;
            } else if ((lead & 0xE0) == 0xC0) {
                length = 2;
                code = lead & 0x1F;
            } else if ((lead & 0xF0) == 0xE0) {
                length = 3;
                code = lead & 0x0F;
            } else if ((lead & 0xF8) == 0xF0) {
                length = 4;
                code = lead & 0x07;
            } else {
                throwProtocol(QStringLiteral("invalid UTF-8 lead byte"),
                              QStringLiteral("offset %1").arg(index));
            }
            if (index + length > bytes.size())
                throwProtocol(QStringLiteral("truncated UTF-8 sequence"));
            for (int extra = 1; extra < length; ++extra) {
                const quint8 continuation = static_cast<quint8>(bytes.at(index + extra));
                if ((continuation & 0xC0) != 0x80)
                    throwProtocol(QStringLiteral("invalid UTF-8 continuation byte"));
                code = (code << 6) | (continuation & 0x3F);
            }
            if (length == 2 && code < 0x80)
                throwProtocol(QStringLiteral("overlong UTF-8 sequence"));
            if (length == 3 && code < 0x800)
                throwProtocol(QStringLiteral("overlong UTF-8 sequence"));
            if (length == 4 && code < 0x10000)
                throwProtocol(QStringLiteral("overlong UTF-8 sequence"));
            if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
                throwProtocol(QStringLiteral("invalid UTF-8 code point"));
            if (code > 0xFFFF) {
                const char32_t adjusted = code - 0x10000;
                text.append(QChar(static_cast<char16_t>(0xD800 + (adjusted >> 10))));
                text.append(QChar(static_cast<char16_t>(0xDC00 + (adjusted & 0x3FF))));
            } else {
                text.append(QChar(static_cast<char16_t>(code)));
            }
            index += length;
        }
        return text;
    }
    case Kind::Latin1:
    case Kind::Ascii: {
        QString text;
        text.reserve(bytes.size());
        for (const char raw : bytes) {
            const quint8 byte = static_cast<quint8>(raw);
            if (m_kind == Kind::Ascii && byte > 0x7F)
                throwProtocol(QStringLiteral("byte is outside ASCII"),
                              QStringLiteral("0x%1").arg(byte, 2, 16, QLatin1Char('0')));
            text.append(QChar(byte));
        }
        return text;
    }
    case Kind::Cp1252: {
        QString text;
        text.reserve(bytes.size());
        for (const char raw : bytes) {
            const quint8 byte = static_cast<quint8>(raw);
            const std::optional<char32_t> mapped = cp1252ToUnicode(byte);
            if (!mapped.has_value())
                throwProtocol(QStringLiteral("byte 0x%1 is undefined in cp1252")
                                  .arg(byte, 2, 16, QLatin1Char('0')),
                              bytesToHexPreview(bytes));
            text.append(QChar(static_cast<char16_t>(*mapped)));
        }
        return text;
    }
    }
    throwProtocol(QStringLiteral("unsupported codec"));
}

bool AnsiCodec::canEncode(const QString &text) const
{
    try {
        encode(text);
        return true;
    } catch (const UvException &) {
        return false;
    }
}

} // namespace ultraview::protocol
