#include "protocol/RouteParser.h"

#include <QStringList>

namespace ultraview::protocol {
namespace {

QStringList splitFields(const QByteArray &payload, const AnsiCodec &codec)
{
    return codec.decode(payload).split(QLatin1Char(':'));
}

/// Reference numeric validation: an optional leading '-' followed by ASCII
/// digits only (`not value or not value.lstrip("-").isascii() or not
/// value.lstrip("-").isdigit()`), then a range check.
qint64 parseRouteInteger(const QString &value, qint64 low, qint64 high)
{
    QString digits = value;
    bool negative = false;
    if (digits.startsWith(QLatin1Char('-'))) {
        negative = true;
        digits.remove(0, 1);
    }
    if (digits.isEmpty())
        throwProtocol(QStringLiteral("invalid numeric route field"), value);
    for (const QChar unit : digits) {
        if (unit < QLatin1Char('0') || unit > QLatin1Char('9'))
            throwProtocol(QStringLiteral("invalid numeric route field"), value);
    }
    bool ok = false;
    const qint64 magnitude = digits.toLongLong(&ok);
    if (!ok)
        throwProtocol(QStringLiteral("invalid numeric route field"), value);
    const qint64 number = negative ? -magnitude : magnitude;
    if (number < low || number > high)
        throwProtocol(QStringLiteral("route field outside modeled range"), value);
    return number;
}

} // namespace

TargetRoute RouteParser::parseRoute(const QByteArray &payload, const AnsiCodec &codec)
{
    const QStringList fields = splitFields(payload, codec);
    if (fields.size() < 15 || fields.at(0) != QLatin1String("8") || fields.at(1) != QLatin1String("1"))
        throwProtocol(QStringLiteral("expected status1 command8 route response"));

    TargetRoute route;
    route.lanIp = fields.at(2);
    route.publicIp = fields.at(4);
    route.publicPort = static_cast<quint16>(parseRouteInteger(fields.at(5), 0, 65535));
    route.lanPort = static_cast<quint16>(parseRouteInteger(fields.at(6), 0, 65535));
    route.commandPort = static_cast<quint16>(parseRouteInteger(fields.at(7), 0, 65535));
    route.computerNameEncoded = fields.at(8);
    route.tcpPort = static_cast<quint16>(parseRouteInteger(fields.at(9), 0, 65535));
    route.tcpServerIp = fields.at(11);
    route.matchedCustomPassword = static_cast<qint32>(parseRouteInteger(fields.at(12), -32768, 32767));
    route.targetSessionId = static_cast<qint32>(parseRouteInteger(fields.at(13), -2147483648LL, 2147483647LL));
    route.tcpServerHostname = fields.at(14);
    if (fields.size() > 20)
        route.partnerSecretToken = fields.at(20);
    return route;
}

bool RouteParser::isFailureStatus(const QByteArray &payload, const AnsiCodec &codec)
{
    if (!payload.startsWith("8:"))
        return false;
    const QStringList fields = splitFields(payload, codec);
    if (fields.size() < 2)
        return false;
    if (fields.at(1) == QLatin1String("1"))
        return false;
    // A status-1 route reply always carries at least 15 fields; anything shorter
    // that still starts with "8:" is a refusal.
    return true;
}

QString RouteParser::statusField(const QByteArray &payload, const AnsiCodec &codec)
{
    const QStringList fields = splitFields(payload, codec);
    return fields.size() > 1 ? fields.at(1) : QString();
}

UvException RouteParser::authenticationError(const QString &status)
{
    if (status == QLatin1String("2")) {
        return UvException(ErrorCode::Authentication,
                           QStringLiteral("wrong password"),
                           QStringLiteral("the partner refused the supplied password (status 2)"));
    }
    if (status == QLatin1String("11")) {
        return UvException(ErrorCode::Authentication,
                           QStringLiteral("partner is offline or the ID does not exist"),
                           QStringLiteral("status 11"));
    }
    if (status == QLatin1String("99")) {
        return UvException(ErrorCode::Network,
                           QStringLiteral("cannot connect to the partner"),
                           QStringLiteral("status 99"));
    }
    return UvException(ErrorCode::Authentication, QStringLiteral("connection refused by the server"),
                       QStringLiteral("status %1").arg(status));
}

} // namespace ultraview::protocol
