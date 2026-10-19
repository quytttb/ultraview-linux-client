#include "protocol/PacketCodec.h"

#include "core/ErrorTypes.h"

#include <QList>
#include <QtEndian>

namespace ultraview::protocol {
namespace {

constexpr int kLengthPrefixBytes = 4;
constexpr char kHttpHeaderEnd[] = "\r\n\r\n";
constexpr int kHttpHeaderEndBytes = 4;

quint32 readLittleEndianUInt32(const char *data)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data));
}

} // namespace

FrameStream::FrameStream(bool expectHttpHeader, quint32 maxFrameBytes)
    : m_expectHttpHeader(expectHttpHeader)
{
    setMaxFrameBytes(maxFrameBytes);
}

void FrameStream::setExpectHttpHeader(bool expect)
{
    m_expectHttpHeader = expect;
}

void FrameStream::setMaxFrameBytes(quint32 limit)
{
    if (limit == 0 || limit > kAbsoluteMaxFrameBytes)
        throwError(ErrorCode::Configuration,
                   QStringLiteral("frame limit must be between 1 and %1 bytes")
                       .arg(kAbsoluteMaxFrameBytes));
    m_maxFrameBytes = limit;
}

void FrameStream::clear()
{
    m_buffer.clear();
}

QByteArray FrameStream::takeHttpHeader()
{
    if (!m_expectHttpHeader)
        return {};
    const qsizetype end = m_buffer.indexOf(kHttpHeaderEnd, 0);
    if (end < 0)
        return {};
    const qsizetype cut = end + kHttpHeaderEndBytes;
    const QByteArray header = m_buffer.left(cut);
    m_buffer.remove(0, cut);
    m_expectHttpHeader = false;
    return header;
}

QList<QByteArray> FrameStream::feed(const QByteArray &chunk)
{
    if (!chunk.isEmpty())
        m_buffer.append(chunk);

    if (m_expectHttpHeader) {
        const qsizetype end = m_buffer.indexOf(kHttpHeaderEnd, 0);
        if (end < 0)
            return {};
        m_buffer.remove(0, end + kHttpHeaderEndBytes);
        m_expectHttpHeader = false;
    }

    QList<QByteArray> frames;
    // Reference guard is a strict '>' comparison; see the header comment.
    while (m_buffer.size() > kLengthPrefixBytes) {
        const quint32 length = readLittleEndianUInt32(m_buffer.constData());
        if (length > m_maxFrameBytes) {
            // The declared length is already known to be unacceptable, so the
            // buffered bytes can never form a frame we would deliver. Drop them
            // instead of letting the buffer grow until the limit is reached.
            m_buffer.clear();
            throwResource(QStringLiteral("inbound frame exceeds the configured limit"),
                          QStringLiteral("%1 > %2 bytes").arg(length).arg(m_maxFrameBytes));
        }
        const qint64 needed = qint64{length} + kLengthPrefixBytes;
        if (m_buffer.size() < needed)
            break;
        frames.append(m_buffer.mid(kLengthPrefixBytes, static_cast<int>(length)));
        m_buffer.remove(0, static_cast<int>(needed));
    }
    return frames;
}

qint64 FrameStream::pendingFrameBytes() const
{
    if (m_buffer.size() <= kLengthPrefixBytes)
        return m_buffer.size();
    const quint32 length = readLittleEndianUInt32(m_buffer.constData());
    return qMin<qint64>(m_buffer.size(), qint64{length} + kLengthPrefixBytes);
}

QByteArray encodeTransportFrame(const QByteArray &body)
{
    // The reference writes Int32 and rejects bodies above 0x7fffffff - 4.
    constexpr qint64 kMaxManagedLength = 0x7fffffffLL - 4;
    if (body.size() > kMaxManagedLength)
        throwResource(QStringLiteral("frame exceeds the checked managed allocation size"),
                      QStringLiteral("%1 bytes").arg(body.size()));
    QByteArray out;
    out.reserve(kLengthPrefixBytes + body.size());
    const quint32 length = static_cast<quint32>(body.size());
    char prefix[kLengthPrefixBytes];
    qToLittleEndian<quint32>(length, reinterpret_cast<uchar *>(prefix));
    out.append(prefix, kLengthPrefixBytes);
    out.append(body);
    return out;
}

QByteArray encodeTypedTransportFrame(const QByteArray &payload, quint8 kind)
{
    QByteArray body;
    body.reserve(1 + payload.size());
    body.append(static_cast<char>(kind));
    body.append(payload);
    return encodeTransportFrame(body);
}

bool parseCanonicalIpv4(const QString &ipv4, quint32 *addressValue)
{
    const QStringList parts = ipv4.split(QLatin1Char('.'));
    if (parts.size() != 4)
        return false;
    quint32 value = 0;
    for (int index = 0; index < 4; ++index) {
        const QString &part = parts.at(index);
        if (part.isEmpty() || part.size() > 3)
            return false;
        for (const QChar unit : part) {
            if (unit < QLatin1Char('0') || unit > QLatin1Char('9'))
                return false;
        }
        bool ok = false;
        const int octet = part.toInt(&ok);
        if (!ok || octet > 255)
            return false;
        value |= (static_cast<quint32>(octet) << (8 * (3 - index)));
    }
    if (addressValue)
        *addressValue = value;
    return true;
}

QString formatIpv4(quint32 addressValue)
{
    return QStringLiteral("%1.%2.%3.%4")
        .arg((addressValue >> 24) & 0xFF)
        .arg((addressValue >> 16) & 0xFF)
        .arg((addressValue >> 8) & 0xFF)
        .arg(addressValue & 0xFF);
}

QByteArray encodeRelayFrame(const QByteArray &payload, const QString &ipv4, quint16 port, quint8 flag)
{
    quint32 address = 0;
    if (!parseCanonicalIpv4(ipv4, &address))
        throwError(ErrorCode::Configuration,
                   QStringLiteral("expected four decimal IPv4 octets"), ipv4);

    QByteArray body;
    body.reserve(8 + payload.size());
    body.append(static_cast<char>(0x00));            // kind: relay
    body.append(static_cast<char>(flag));            // 1 when the payload is UTF-16LE
    char addressBytes[4];
    qToLittleEndian<quint32>(address, reinterpret_cast<uchar *>(addressBytes));
    body.append(addressBytes, 4);
    char portBytes[2];
    qToLittleEndian<quint16>(port, reinterpret_cast<uchar *>(portBytes));
    body.append(portBytes, 2);
    body.append(payload);
    return encodeTransportFrame(body);
}

TransportBody decodeTransportBody(const QByteArray &body)
{
    TransportBody result;
    if (body.isEmpty())
        throwProtocol(QStringLiteral("empty transport body"));

    const quint8 kind = static_cast<quint8>(body.at(0));
    if (kind == 0x00) {
        if (body.size() < 8)
            throwProtocol(QStringLiteral("truncated relay header"),
                          QStringLiteral("%1 bytes").arg(body.size()));
        result.kind = TransportBody::Kind::Relay;
        result.flag = body.at(1) != 0;
        result.addressValue = readLittleEndianUInt32(body.constData() + 2);
        result.port = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(body.constData() + 6));
        result.data = body.mid(8);
        return result;
    }
    if (kind == 0x01) {
        result.kind = TransportBody::Kind::Direct;
        result.flag = false;
        result.data = body.mid(1);
        return result;
    }
    result.kind = TransportBody::Kind::Unknown;
    return result;
}

QByteArray httpServicePreface(const QString &host, bool post)
{
    // Field order, casing, CRLF terminators and the literal content length are
    // copied from the artifact; do not "clean up" this string.
    QByteArray out;
    out.reserve(256);
    out.append(post ? "POST" : "GET");
    out.append(" /UVService/ HTTP/1.1\r\n"
               "User-Agent: Mozilla/4.0 (compatible; MSIE5.01; Windows NT)\r\n"
               "Content-Type: application/octet-stream\r\n"
               "Connection: Keep-Alive\r\n"
               "Content-length: ");
    out.append(QByteArray::number(kHttpPrefaceContentLength));
    out.append("\r\nHost: ");
    out.append(host.toLatin1());
    out.append("\r\n\r\n");
    return out;
}

QByteArray encodeByteArrayFields(const QList<QByteArray> &fields)
{
    qint64 total = 0;
    for (const QByteArray &field : fields) {
        total += 4 + field.size();
        if (total > kAbsoluteMaxFrameBytes)
            throwResource(QStringLiteral("field array exceeds the frame limit"),
                          QStringLiteral("%1 bytes").arg(total));
    }
    QByteArray out;
    out.reserve(static_cast<int>(total));
    for (const QByteArray &field : fields) {
        char prefix[kLengthPrefixBytes];
        qToLittleEndian<quint32>(static_cast<quint32>(field.size()), reinterpret_cast<uchar *>(prefix));
        out.append(prefix, kLengthPrefixBytes);
        out.append(field);
    }
    return out;
}

QList<QByteArray> decodeByteArrayFields(const QByteArray &payload, int expectedCount)
{
    QList<QByteArray> fields;
    qint64 offset = 0;
    const qint64 size = payload.size();
    while (offset < size) {
        if (size - offset < kLengthPrefixBytes)
            throwProtocol(QStringLiteral("truncated field length"));
        const quint32 length = readLittleEndianUInt32(payload.constData() + offset);
        offset += kLengthPrefixBytes;
        // `length > len(payload) - offset` in the reference; both sides are
        // checked integers here, so no overflow is possible.
        if (qint64{length} > size - offset)
            throwProtocol(QStringLiteral("truncated field value"));
        fields.append(payload.mid(static_cast<int>(offset), static_cast<int>(length)));
        offset += length;
    }
    if (expectedCount >= 0 && fields.size() != expectedCount)
        throwProtocol(QStringLiteral("unexpected field count"),
                      QStringLiteral("expected %1, got %2").arg(expectedCount).arg(fields.size()));
    return fields;
}

} // namespace ultraview::protocol
