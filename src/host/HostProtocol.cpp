#include "host/HostProtocol.h"

#include <QCoreApplication>
#include "core/ErrorTypes.h"
#include "host/HostProfile.h"
#include "protocol/Digest.h"
#include "protocol/PacketCodec.h"

#include <QtEndian>

#include <zstd.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace ultraview::host {
namespace {

bool isAsciiPrintable(const QByteArray &value)
{
    for (const char unit : value) {
        const uchar byte = static_cast<uchar>(unit);
        if (byte < 32 || byte > 126)
            return false;
    }
    return true;
}

bool isAsciiDigits(const QString &value)
{
    if (value.isEmpty())
        return false;
    for (const QChar unit : value) {
        if (unit < QLatin1Char('0') || unit > QLatin1Char('9'))
            return false;
    }
    return true;
}

bool isLowerHex(const QString &value, int size)
{
    if (value.size() != size)
        return false;
    for (const QChar unit : value) {
        const ushort code = unit.unicode();
        if (!((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f')))
            return false;
    }
    return true;
}

std::optional<qint64> strictInteger(const QString &text, qint64 minimum, qint64 maximum)
{
    if (text.isEmpty())
        return std::nullopt;
    qsizetype index = 0;
    bool negative = false;
    if (text.at(0) == QLatin1Char('-') || text.at(0) == QLatin1Char('+')) {
        negative = text.at(0) == QLatin1Char('-');
        index = 1;
    }
    if (index >= text.size())
        return std::nullopt;
    quint64 value = 0;
    for (; index < text.size(); ++index) {
        const QChar unit = text.at(index);
        if (unit < QLatin1Char('0') || unit > QLatin1Char('9'))
            return std::nullopt;
        const int digit = unit.unicode() - '0';
        const quint64 limit = negative
            ? static_cast<quint64>(std::numeric_limits<qint64>::max()) + 1
            : static_cast<quint64>(std::numeric_limits<qint64>::max());
        if (value > (limit - static_cast<quint64>(digit)) / 10)
            return std::nullopt;
        value = value * 10 + static_cast<quint64>(digit);
    }
    qint64 signedValue = 0;
    if (negative) {
        if (value == static_cast<quint64>(std::numeric_limits<qint64>::max()) + 1)
            signedValue = std::numeric_limits<qint64>::min();
        else
            signedValue = -static_cast<qint64>(value);
    } else {
        signedValue = static_cast<qint64>(value);
    }
    if (signedValue < minimum || signedValue > maximum)
        return std::nullopt;
    return signedValue;
}

std::optional<quint64> strictUnsigned(const QString &text, quint64 maximum)
{
    if (text.isEmpty())
        return std::nullopt;
    quint64 value = 0;
    for (const QChar unit : text) {
        if (unit < QLatin1Char('0') || unit > QLatin1Char('9'))
            return std::nullopt;
        const quint64 digit = static_cast<quint64>(unit.unicode() - '0');
        if (value > (std::numeric_limits<quint64>::max() - digit) / 10)
            return std::nullopt;
        value = value * 10 + digit;
    }
    if (value > maximum)
        return std::nullopt;
    return value;
}

bool constantTimeEqual(const QString &left, const QString &right)
{
    if (left.size() != right.size())
        return false;
    uchar difference = 0;
    for (qsizetype index = 0; index < left.size(); ++index)
        difference |= static_cast<uchar>(left.at(index).unicode() ^ right.at(index).unicode());
    return difference == 0;
}

QByteArray int32le(qint32 value)
{
    QByteArray out(4, '\0');
    qToLittleEndian<qint32>(value, reinterpret_cast<uchar *>(out.data()));
    return out;
}

QByteArray uint16le(quint16 value)
{
    QByteArray out(2, '\0');
    qToLittleEndian<quint16>(value, reinterpret_cast<uchar *>(out.data()));
    return out;
}

QStringList splitFields(const QByteArray &payload)
{
    return QString::fromLatin1(payload).split(QLatin1Char(':'));
}

bool endpointHostnameValid(QString hostname)
{
    if (hostname.isEmpty())
        hostname = QStringLiteral("functions3.ultraviewer.net");
    hostname = hostname.toLower();
    if (hostname.size() > 253)
        return false;
    for (const QChar unit : hostname) {
        const ushort code = unit.unicode();
        if (!((code >= 'a' && code <= 'z') || (code >= '0' && code <= '9')
              || code == '-' || code == '.'))
            return false;
    }
    return hostname == QLatin1String("ultraviewer.net")
        || hostname.endsWith(QLatin1String(".ultraviewer.net"));
}

std::optional<RelayRequest> makeRelayEndpoint(const QString &address, const QString &hostname,
                                               const QString &tag, const QString &hash)
{
    quint32 parsed = 0;
    if (!protocol::parseCanonicalIpv4(address, &parsed))
        return std::nullopt;
    if (!endpointHostnameValid(hostname))
        return std::nullopt;
    RelayRequest request;
    request.address = address;
    request.hostname = hostname.isEmpty() ? QStringLiteral("functions3.ultraviewer.net")
                                         : hostname.toLower();
    request.tag = tag;
    request.hash = hash;
    return request;
}

QByteArray compressZstd(const QByteArray &input)
{
    const qint64 maxOutput = static_cast<qint64>(kMaxPayload) - (44 + 1 + 8 + 4);
    if (input.isEmpty() || input.size() > kMaxPayload)
        throwError(ErrorCode::Resource, QCoreApplication::translate("HostProtocol", "screen frame exceeds the encoder limit"));
    const size_t bound = ZSTD_compressBound(static_cast<size_t>(input.size()));
    const size_t capacity = static_cast<size_t>(qMin<qint64>(
        static_cast<qint64>(bound), maxOutput));
    QByteArray output;
    output.resize(static_cast<int>(capacity));
    ZSTD_CCtx *context = ZSTD_createCCtx();
    if (context == nullptr)
        throwError(ErrorCode::Resource, QCoreApplication::translate("HostProtocol", "cannot create a Zstandard compression context"));
    const size_t produced = ZSTD_compressCCtx(context, output.data(), capacity,
                                             input.constData(), static_cast<size_t>(input.size()), 1);
    ZSTD_freeCCtx(context);
    if (ZSTD_isError(produced)) {
        throwError(ErrorCode::Resource, QCoreApplication::translate("HostProtocol", "Zstandard compression failed"),
                   QString::fromUtf8(ZSTD_getErrorName(produced)));
    }
    if (static_cast<qint64>(produced) > maxOutput)
        throwError(ErrorCode::Resource, QCoreApplication::translate("HostProtocol", "compressed screen frame is too large"));
    output.resize(static_cast<int>(produced));
    return output;
}

} // namespace

bool HostFrame::sameLayout(const HostFrame &other) const
{
    return width == other.width && height == other.height && monitors == other.monitors
        && layoutId == other.layoutId;
}

bool HostFrame::isValid() const
{
    if (width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension)
        return false;
    return rgb555BottomUp.size() == static_cast<qsizetype>(width) * height * 2;
}

QByteArray buildRegistrationCommand(const HostIdentity &identity)
{
    const QStringList terms = identity.registrationTerms();
    if (terms.size() != 28)
        throwError(ErrorCode::Configuration, QCoreApplication::translate("HostProtocol", "host registration needs 28 fields"));
    for (int index = 0; index < terms.size(); ++index) {
        const QString &term = terms.at(index);
        for (const QChar unit : term) {
            const ushort code = unit.unicode();
            if (code < 32 || code > 126 || code == ':')
                throwError(ErrorCode::Configuration,
                           QCoreApplication::translate("HostProtocol", "host registration field %1 is not printable ASCII")
                               .arg(index));
        }
    }
    return terms.join(QLatin1Char(':')).toLatin1();
}

RegistrationReply parseRegistrationReply(const QByteArray &payload)
{
    if (payload.isEmpty() || payload.size() > 65536)
        throwProtocol(QCoreApplication::translate("HostProtocol", "host registration reply has an invalid size"));
    if (!isAsciiPrintable(payload))
        throwProtocol(QCoreApplication::translate("HostProtocol", "host registration reply is not printable ASCII"));
    const QStringList fields = splitFields(payload);
    if (fields.size() < 11 || fields.size() > 128)
        throwProtocol(QCoreApplication::translate("HostProtocol", "host registration reply has the wrong field count"));
    if (fields.at(0) != QLatin1String("3"))
        throwProtocol(QCoreApplication::translate("HostProtocol", "not a command 3 reply"));

    if (!isAsciiDigits(fields.at(1)))
        throwProtocol(QCoreApplication::translate("HostProtocol", "host registration reply has no public port"));
    const auto port = strictUnsigned(fields.at(1), 65535);
    if (!port.has_value() || *port == 0)
        throwProtocol(QCoreApplication::translate("HostProtocol", "host registration reply has an invalid public port"));
    if (!isAsciiDigits(fields.at(2)))
        throwProtocol(QCoreApplication::translate("HostProtocol", "host registration reply has no assigned ID"));
    const auto id = strictUnsigned(fields.at(2), std::numeric_limits<quint64>::max());
    if (!id.has_value() || *id == 0)
        throwProtocol(QCoreApplication::translate("HostProtocol", "host registration reply has an invalid assigned ID"));
    quint32 address = 0;
    if (!protocol::parseCanonicalIpv4(fields.at(3), &address))
        throwProtocol(QCoreApplication::translate("HostProtocol", "host registration reply has an invalid public address"));
    const auto status = strictInteger(fields.at(7), std::numeric_limits<qint32>::min(),
                                      std::numeric_limits<qint32>::max());
    if (!status.has_value())
        throwProtocol(QCoreApplication::translate("HostProtocol", "host registration reply has an invalid status"));

    RegistrationReply reply;
    reply.publicPort = static_cast<quint16>(*port);
    reply.id = QString::number(*id);
    reply.publicIp = fields.at(3);
    reply.status = static_cast<qint32>(*status);
    reply.fields = fields;
    reply.pingList = fields.at(6);
    reply.pingHostNames = fields.at(9);
    reply.serverToken = fields.at(10);
    return reply;
}

std::optional<RelayRequest> parseRelayRequest(const QByteArray &payload,
                                              const QString &defaultAddresses,
                                              const QString &defaultHostNames)
{
    if (payload.isEmpty() || payload.size() > 16384 || !isAsciiPrintable(payload))
        return std::nullopt;
    const QStringList fields = splitFields(payload);
    if (fields.size() >= 4 && fields.at(0) == QLatin1String("11"))
        return makeRelayEndpoint(fields.at(1), fields.at(3), QString(), QString());

    const auto triState = [](const QString &value) {
        return value == QLatin1String("0") || value == QLatin1String("1")
            || value == QLatin1String("-1");
    };
    if (!(fields.size() >= 9 && fields.at(0) == QLatin1String("17")
          && fields.at(1).size() <= 512 && fields.at(2).size() <= 512
          && triState(fields.at(5)) && strictInteger(fields.at(6), -32768, 32767).has_value()
          && strictInteger(fields.at(7), std::numeric_limits<qint32>::min(),
                           std::numeric_limits<qint32>::max()).has_value()
          && triState(fields.at(8)))) {
        return std::nullopt;
    }

    const QStringList addresses =
        (fields.at(3).isEmpty() ? defaultAddresses : fields.at(3)).split(QLatin1Char(';'));
    const QStringList hostnames =
        (fields.at(4).isEmpty() ? defaultHostNames : fields.at(4)).split(QLatin1Char(';'));
    if (addresses.size() > 64 || hostnames.size() > 64)
        return std::nullopt;
    for (int index = 0; index < addresses.size(); ++index) {
        const auto request = makeRelayEndpoint(addresses.at(index),
                                               hostnames.value(index),
                                               fields.at(1), fields.at(2));
        if (request.has_value())
            return request;
    }
    return std::nullopt;
}

std::optional<RelayAddress> parseRelayAddress(const QByteArray &payload)
{
    if (payload.isEmpty() || payload.size() > 16384 || !isAsciiPrintable(payload))
        return std::nullopt;
    const QStringList fields = splitFields(payload);
    if (fields.size() < 3 || fields.at(0) != QLatin1String("0")
        || !isAsciiDigits(fields.at(1))) {
        return std::nullopt;
    }
    const auto port = strictInteger(fields.at(1), 1, 65535);
    quint32 address = 0;
    if (!port.has_value() || !protocol::parseCanonicalIpv4(fields.at(2), &address))
        return std::nullopt;
    return RelayAddress{static_cast<quint16>(*port), fields.at(2)};
}

QByteArray relayAvailability(const RelayRequest &request, const RelayAddress &address)
{
    return QStringLiteral("17:%1:%2:%3:%4:%5:0:|0")
        .arg(request.tag)
        .arg(request.hash)
        .arg(request.address)
        .arg(address.port)
        .arg(address.address)
        .toLatin1();
}

std::optional<QByteArray> peerPayload(const QByteArray &payload, bool unicode)
{
    if (!unicode)
        return payload;
    if (payload.size() > 2 * 1024 * 1024 || (payload.size() % 2) != 0)
        return std::nullopt;
    QVector<char16_t> units;
    units.reserve(payload.size() / 2);
    for (qsizetype index = 0; index < payload.size(); index += 2) {
        const uchar low = static_cast<uchar>(payload.at(index));
        const uchar high = static_cast<uchar>(payload.at(index + 1));
        units.append(static_cast<char16_t>(low | (static_cast<quint16>(high) << 8)));
    }
    const QString text = QString::fromUtf16(units.constData(), units.size());
    for (const QChar unit : text) {
        const ushort code = unit.unicode();
        if (code < 32 && code != '\t' && code != '\n' && code != '\r')
            return std::nullopt;
    }
    return text.toUtf8();
}

std::optional<LoginRequest> parseLogin(const QByteArray &payload, QString *error)
{
    const auto fail = [error](const QString &message) -> std::optional<LoginRequest> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    if (payload.size() > 16384)
        return fail(QCoreApplication::translate("HostProtocol", "viewer login is too large"));
    if (payload.isEmpty() || !isAsciiPrintable(payload))
        return fail(QCoreApplication::translate("HostProtocol", "viewer login is not printable ASCII"));
    int colons = 0;
    for (const char unit : payload) {
        if (unit == ':')
            ++colons;
    }
    if (colons != 22)
        return fail(QCoreApplication::translate("HostProtocol", "viewer login has the wrong field count"));
    const QStringList fields = splitFields(payload);
    for (const QString &field : fields) {
        if (field.size() > 4096)
            return fail(QCoreApplication::translate("HostProtocol", "viewer login contains an oversized field"));
    }
    if (fields.value(0) != QLatin1String("login"))
        return fail(QCoreApplication::translate("HostProtocol", "not a viewer login"));
    if (!isAsciiDigits(fields.value(1)) || fields.value(1).size() != 8)
        return fail(QCoreApplication::translate("HostProtocol", "viewer login has an invalid nonce"));
    if (!isLowerHex(fields.value(2), 32))
        return fail(QCoreApplication::translate("HostProtocol", "viewer login has an invalid password proof"));
    if (!isAsciiDigits(fields.value(10)))
        return fail(QCoreApplication::translate("HostProtocol", "viewer login has an invalid claimed ID"));
    const auto id = strictUnsigned(fields.value(10), std::numeric_limits<quint64>::max());
    if (!id.has_value() || *id == 0)
        return fail(QCoreApplication::translate("HostProtocol", "viewer login has an invalid claimed ID"));
    const auto width = strictInteger(fields.value(3), 1, std::numeric_limits<qint32>::max());
    const auto height = strictInteger(fields.value(4), 1, std::numeric_limits<qint32>::max());
    if (!width.has_value() || !height.has_value())
        return fail(QCoreApplication::translate("HostProtocol", "viewer login has an invalid screen size"));
    const auto control = strictInteger(fields.value(12), std::numeric_limits<qint16>::min(),
                                       std::numeric_limits<qint16>::max());
    const auto colors = strictInteger(fields.value(16), std::numeric_limits<qint32>::min(),
                                      std::numeric_limits<qint32>::max());
    const auto quality = strictInteger(fields.value(17), std::numeric_limits<qint32>::min(),
                                       std::numeric_limits<qint32>::max());
    if (!control.has_value() || !colors.has_value() || !quality.has_value())
        return fail(QCoreApplication::translate("HostProtocol", "viewer login has an invalid numeric field"));

    LoginRequest login;
    login.fields = fields;
    login.screenWidth = static_cast<qint32>(*width);
    login.screenHeight = static_cast<qint32>(*height);
    login.controlMode = static_cast<qint32>(*control);
    login.displayColors = static_cast<qint32>(*colors);
    login.displayQuality = static_cast<qint32>(*quality);
    return login;
}

bool verifyRandomPassword(const LoginRequest &login, const QString &password)
{
    if (!HostProfile::isAcceptablePassword(password))
        return false;
    const QString expected = protocol::md5HexLower((login.nonce() + password).toUtf8());
    return constantTimeEqual(expected, login.fields.value(2));
}

QString customPasswordProof(const QString &password)
{
    if (!HostProfile::isAcceptablePassword(password))
        return {};
    return protocol::md5HexLower((QString(32, QLatin1Char(' ')) + password).toUtf8());
}

bool verifyCustomProof(const LoginRequest &login, const QString &proof)
{
    if (!isLowerHex(proof, 32) || !isLowerHex(login.fields.value(13), 32))
        return false;
    return constantTimeEqual(proof, login.fields.value(13));
}

bool LoginThrottle::isLocked(qint64 nowMs) const
{
    return m_lockedUntilMs >= 0 && nowMs < m_lockedUntilMs;
}

bool LoginThrottle::recordFailure(qint64 nowMs)
{
    if (m_lockedUntilMs >= 0 && nowMs >= m_lockedUntilMs) {
        m_lockedUntilMs = -1;
        m_failures = 0;
    }
    if (m_lockedUntilMs >= 0)
        return false;
    ++m_failures;
    if (m_failures < kMaxFailures)
        return false;
    m_lockedUntilMs = nowMs + kLockoutMs;
    return true;
}

void LoginThrottle::recordSuccess()
{
    m_failures = 0;
    m_lockedUntilMs = -1;
}

std::optional<HostInputEvent> decodeInput(const QByteArray &payload)
{
    if (payload.size() >= 4 && static_cast<uchar>(payload.at(0)) == 30
        && payload.at(1) == '|' && payload.at(2) == '|' && payload.at(3) == '|') {
        if (payload.size() != 11 || static_cast<uchar>(payload.at(4)) < 1
            || static_cast<uchar>(payload.at(4)) > 4
            || static_cast<uchar>(payload.at(10)) > 1) {
            return std::nullopt;
        }
        HostInputEvent event;
        event.type = HostInputType::Keyboard;
        event.kind = static_cast<quint8>(payload.at(4));
        event.virtualKey = static_cast<quint8>(payload.at(5));
        event.scanCode = static_cast<quint16>(static_cast<uchar>(payload.at(6)))
            | (static_cast<quint16>(static_cast<uchar>(payload.at(7))) << 8);
        event.character = static_cast<quint16>(static_cast<uchar>(payload.at(8)))
            | (static_cast<quint16>(static_cast<uchar>(payload.at(9))) << 8);
        return event;
    }

    if (!payload.startsWith("8|||") || payload.size() > 256 || !isAsciiPrintable(payload))
        return std::nullopt;
    const QString text = QString::fromLatin1(payload);
    const QStringList fields = text.split(QStringLiteral("|||"));
    if (fields.size() < 2)
        return std::nullopt;
    const auto message = strictInteger(fields.at(1), 0, std::numeric_limits<quint32>::max());
    if (!message.has_value())
        return std::nullopt;

    if (fields.size() == 5 && (*message == 0x100 || *message == 0x101
                               || *message == 0x104 || *message == 0x105)
        && fields.at(3) == QLatin1String("-1") && fields.at(4) == QLatin1String("-1")) {
        const auto key = strictInteger(fields.at(2), 0, 255);
        if (!key.has_value())
            return std::nullopt;
        HostInputEvent event;
        event.type = HostInputType::Keyboard;
        event.kind = *message == 0x100 ? 1 : (*message == 0x101 ? 2 : (*message == 0x104 ? 3 : 4));
        event.virtualKey = static_cast<quint8>(*key);
        return event;
    }

    if (*message < 0x200 || *message > 0x20a)
        return std::nullopt;
    const qsizetype expected = *message == 0x200 ? 5 : 4;
    if (fields.size() != expected)
        return std::nullopt;
    QList<qint32> values;
    for (qsizetype index = 2; index < fields.size(); ++index) {
        const auto value = strictInteger(fields.at(index), std::numeric_limits<qint32>::min(),
                                         std::numeric_limits<qint32>::max());
        if (!value.has_value())
            return std::nullopt;
        values.append(static_cast<qint32>(*value));
    }
    HostInputEvent event;
    event.type = HostInputType::Mouse;
    event.message = static_cast<quint32>(*message);
    if (*message == 0x200) {
        event.x = values.at(1);
        event.y = values.at(2);
        return event;
    }
    const quint32 packed = static_cast<quint32>(values.at(1));
    event.x = static_cast<qint32>(static_cast<qint16>(packed & 0xffff));
    event.y = static_cast<qint32>(static_cast<qint16>((packed >> 16) & 0xffff));
    if (*message == 0x20a) {
        const quint32 wparam = static_cast<quint32>(values.at(0));
        event.wheel = static_cast<qint16>((wparam >> 16) & 0xffff);
    }
    return event;
}

QByteArray encodeMultipart(const QList<QByteArray> &parts)
{
    return protocol::encodeByteArrayFields(parts);
}

QByteArray initialization(int width, int height, const QString &version,
                          const QVector<HostMonitor> &monitors)
{
    if (width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension)
        throwError(ErrorCode::Configuration, QCoreApplication::translate("HostProtocol", "invalid host screen geometry"));
    const QStringList components = version.split(QLatin1Char('.'));
    if (components.size() != 3)
        throwError(ErrorCode::Configuration, QCoreApplication::translate("HostProtocol", "host version needs three components"));
    for (const QString &component : components) {
        if (!isAsciiDigits(component)
            || strictInteger(component, 0, std::numeric_limits<qint64>::max()).value_or(-1) < 0) {
            throwError(ErrorCode::Configuration, QCoreApplication::translate("HostProtocol", "invalid host version"));
        }
    }
    if (strictInteger(components.at(0), 0, std::numeric_limits<qint64>::max()).value_or(0) < 6)
        throwError(ErrorCode::Configuration, QCoreApplication::translate("HostProtocol", "host version must be at least 6.0.0"));
    if (monitors.size() > 64)
        throwError(ErrorCode::Resource, QCoreApplication::translate("HostProtocol", "too many monitor rectangles"));

    QString monitorText;
    if (monitors.isEmpty()) {
        monitorText = QStringLiteral("0:0:%1:%2|").arg(width).arg(height);
    } else {
        for (const HostMonitor &monitor : monitors) {
            if (monitor.left < 0 || monitor.top < 0 || monitor.right <= monitor.left
                || monitor.bottom <= monitor.top || monitor.right > width
                || monitor.bottom > height) {
                throwError(ErrorCode::Configuration,
                           QCoreApplication::translate("HostProtocol", "monitor rectangle lies outside the host canvas"));
            }
            monitorText += QStringLiteral("%1:%2:%3:%4|")
                               .arg(monitor.left)
                               .arg(monitor.top)
                               .arg(monitor.right)
                               .arg(monitor.bottom);
        }
    }

    return encodeMultipart({
        QByteArray(1, '\0'),
        int32le(width),
        int32le(height),
        QByteArray(),
        QByteArray(),
        QByteArray(1, '\0'),
        version.toLatin1(),
        QByteArray(1, '\x01'),
        monitorText.toLatin1(),
        int32le(0),
        int32le(0),
    });
}

QByteArray fullFrame(const HostFrame &frame, qint32 sequence)
{
    if (!frame.isValid())
        throwError(ErrorCode::Configuration, QCoreApplication::translate("HostProtocol", "invalid host frame"));
    const qsizetype rowBytes = static_cast<qsizetype>(frame.width) * 2;
    QByteArray topDown;
    topDown.reserve(frame.rgb555BottomUp.size());
    for (qsizetype row = frame.height - 1; row >= 0; --row)
        topDown.append(frame.rgb555BottomUp.constData() + row * rowBytes, rowBytes);

    QByteArray rectangle;
    rectangle.reserve(8);
    rectangle.append(uint16le(0));
    rectangle.append(uint16le(0));
    rectangle.append(uint16le(static_cast<quint16>(frame.width)));
    rectangle.append(uint16le(static_cast<quint16>(frame.height)));
    const QByteArray compressed = compressZstd(topDown);
    return encodeMultipart({
        QByteArray(1, '\x01'),
        rectangle,
        compressed,
        QByteArray(),
        int32le(sequence),
        QByteArray(),
        QByteArray(),
        QByteArray(),
        QByteArray(),
        QByteArray(),
        QByteArray(),
    });
}

QByteArray keepaliveReply()
{
    return singleCommand(20);
}

QByteArray controlPermission(bool allowControl)
{
    return encodeMultipart({QByteArray(1, '\x31'),
                            QByteArray(1, allowControl ? '\x00' : '\x01')});
}

QByteArray singleCommand(quint8 command)
{
    return encodeMultipart({QByteArray(1, static_cast<char>(command))});
}

QByteArray rgb555FromBgrx(const uchar *source, int width, int height, int stride, bool rgbOrder)
{
    if (source == nullptr || width <= 0 || height <= 0 || stride < width * 4)
        throwError(ErrorCode::Configuration, QCoreApplication::translate("HostProtocol", "invalid captured image geometry"));
    QByteArray output;
    output.resize(static_cast<qsizetype>(width) * height * 2);
    char *target = output.data();
    for (int y = height - 1; y >= 0; --y) {
        const uchar *row = source + static_cast<qsizetype>(y) * stride;
        for (int x = 0; x < width; ++x) {
            const uchar *pixel = row + x * 4;
            const uchar red = rgbOrder ? pixel[0] : pixel[2];
            const uchar green = pixel[1];
            const uchar blue = rgbOrder ? pixel[2] : pixel[0];
            const quint16 value = static_cast<quint16>(((red >> 3) << 10)
                                                       | ((green >> 3) << 5)
                                                       | (blue >> 3));
            *target++ = static_cast<char>(value & 0xff);
            *target++ = static_cast<char>((value >> 8) & 0xff);
        }
    }
    return output;
}

QByteArray scaleRgb555(const QByteArray &source, int sourceWidth, int sourceHeight,
                       int width, int height)
{
    if (sourceWidth <= 0 || sourceHeight <= 0 || width <= 0 || height <= 0
        || source.size() != static_cast<qsizetype>(sourceWidth) * sourceHeight * 2) {
        throwError(ErrorCode::Configuration, QCoreApplication::translate("HostProtocol", "invalid RGB555 scaling geometry"));
    }
    if (sourceWidth == width && sourceHeight == height)
        return source;

    const auto pixel = [&source, sourceWidth](int x, int y) -> quint32 {
        const qsizetype offset = (static_cast<qsizetype>(y) * sourceWidth + x) * 2;
        return static_cast<quint32>(static_cast<uchar>(source.at(offset)))
            | (static_cast<quint32>(static_cast<uchar>(source.at(offset + 1))) << 8);
    };

    QVector<int> x0(width);
    QVector<int> x1(width);
    for (int x = 0; x < width; ++x) {
        x0[x] = static_cast<int>(static_cast<qint64>(x) * sourceWidth / width);
        x1[x] = std::max(x0[x] + 1, static_cast<int>(static_cast<qint64>(x + 1)
                                                     * sourceWidth / width));
    }

    QByteArray output;
    output.resize(static_cast<qsizetype>(width) * height * 2);
    char *target = output.data();
    for (int y = 0; y < height; ++y) {
        const int sy0 = static_cast<int>(static_cast<qint64>(y) * sourceHeight / height);
        const int sy1 = std::max(sy0 + 1, static_cast<int>(static_cast<qint64>(y + 1)
                                                           * sourceHeight / height));
        for (int x = 0; x < width; ++x) {
            quint64 red = 0;
            quint64 green = 0;
            quint64 blue = 0;
            quint64 count = 0;
            for (int sy = sy0; sy < sy1; ++sy) {
                for (int sx = x0[x]; sx < x1[x]; ++sx) {
                    const quint32 value = pixel(sx, sy);
                    red += (value >> 10) & 31;
                    green += (value >> 5) & 31;
                    blue += value & 31;
                    ++count;
                }
            }
            const quint16 value = static_cast<quint16>((((red + count / 2) / count) << 10)
                                                       | (((green + count / 2) / count) << 5)
                                                       | ((blue + count / 2) / count));
            *target++ = static_cast<char>(value & 0xff);
            *target++ = static_cast<char>((value >> 8) & 0xff);
        }
    }
    return output;
}

} // namespace ultraview::host
