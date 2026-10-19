//
// Parity tests for protocol/RouteParser (command 8 replies and route fields).
//
#include "TestSupport.h"

#include "core/ErrorTypes.h"
#include "protocol/RouteParser.h"

#include <QtTest>

using namespace ultraview;
using namespace ultraview::protocol;

class TestRoute : public QObject {
    Q_OBJECT

private slots:
    void fullRoute();
    void routeWithoutSecretToken();
    void refusalStatuses();
    void malformedRoutes();
    void routePayloadBuilderMatchesFixture();
    void statusMessages();
};

void TestRoute::fullRoute()
{
    const QJsonObject fixture = uvtest::Fixtures::instance().object(QStringLiteral("route"));
    const QByteArray payload = uvtest::Fixtures::entryBytes(fixture, QStringLiteral("payload_hex"));
    const TargetRoute route = RouteParser::parseRoute(payload, AnsiCodec::cp1252());
    QCOMPARE(route.lanIp, fixture.value(QStringLiteral("lan_ip")).toString());
    QCOMPARE(route.publicIp, fixture.value(QStringLiteral("public_ip")).toString());
    QCOMPARE(route.publicPort, static_cast<quint16>(fixture.value(QStringLiteral("public_port")).toInt()));
    QCOMPARE(route.lanPort, static_cast<quint16>(fixture.value(QStringLiteral("lan_port")).toInt()));
    QCOMPARE(route.commandPort, static_cast<quint16>(fixture.value(QStringLiteral("command_port")).toInt()));
    QCOMPARE(route.computerNameEncoded, fixture.value(QStringLiteral("computer_name")).toString());
    QCOMPARE(route.tcpPort, static_cast<quint16>(fixture.value(QStringLiteral("tcp_port")).toInt()));
    QCOMPARE(route.tcpServerIp, fixture.value(QStringLiteral("tcp_server_ip")).toString());
    QCOMPARE(route.matchedCustomPassword, fixture.value(QStringLiteral("matched_custom_password")).toInt());
    QCOMPARE(route.targetSessionId, fixture.value(QStringLiteral("target_session_id")).toInt());
    QCOMPARE(route.tcpServerHostname, fixture.value(QStringLiteral("tcp_server_hostname")).toString());
    QVERIFY(route.partnerSecretToken.has_value());
    QCOMPARE(*route.partnerSecretToken, fixture.value(QStringLiteral("partner_secret_token")).toString());
    QCOMPARE(RouteParser::statusField(payload, AnsiCodec::cp1252()), QStringLiteral("1"));
    QVERIFY(!RouteParser::isFailureStatus(payload, AnsiCodec::cp1252()));
}

void TestRoute::routeWithoutSecretToken()
{
    const QJsonObject fixture = uvtest::Fixtures::instance().object(QStringLiteral("route_short"));
    const QByteArray payload = uvtest::Fixtures::entryBytes(fixture, QStringLiteral("payload_hex"));
    const TargetRoute route = RouteParser::parseRoute(payload, AnsiCodec::cp1252());
    // The reference only reads field 20 when the payload has more than 20 fields.
    QVERIFY(!route.partnerSecretToken.has_value());
    QCOMPARE(route.tcpServerIp, QStringLiteral("relay.example.test"));
    QVERIFY(!RouteParser::isFailureStatus(payload, AnsiCodec::cp1252()));
}

void TestRoute::refusalStatuses()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("route_status"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        const QByteArray payload = uvtest::Fixtures::entryBytes(entry, QStringLiteral("payload_hex"));
        const QString status = entry.value(QStringLiteral("status")).toString();
        QCOMPARE(RouteParser::statusField(payload, AnsiCodec::cp1252()), status);
        QVERIFY(RouteParser::isFailureStatus(payload, AnsiCodec::cp1252()));
        QVERIFY_EXCEPTION_THROWN(RouteParser::parseRoute(payload, AnsiCodec::cp1252()), UvException);
        const UvException exception = RouteParser::authenticationError(status);
        // Status 99 means "cannot connect", which is a network condition; the
        // other refusals are authentication failures.
        QCOMPARE(exception.code(),
                 status == QLatin1String("99") ? ErrorCode::Network : ErrorCode::Authentication);
    }
}

void TestRoute::malformedRoutes()
{
    const AnsiCodec &codec = AnsiCodec::cp1252();
    // A reply that is not command 8, a status-1 reply with too few fields and a
    // numeric field out of range are all protocol violations.
    for (const QByteArray &payload : {QByteArrayLiteral("3:1:2:3"), QByteArrayLiteral("8:1:1.2.3.4"),
                                      QByteArrayLiteral("8:1:1.2.3.4:0:203.0.113.7:99999:1:1:1:1:1:x:1:1:y")}) {
        QVERIFY_EXCEPTION_THROWN(RouteParser::parseRoute(payload, codec), UvException);
    }
    // The status field stays readable for a reply that cannot be a route.
    QCOMPARE(RouteParser::statusField(QByteArrayLiteral("8:2"), codec), QStringLiteral("2"));
    QVERIFY(RouteParser::isFailureStatus(QByteArrayLiteral("8:2:x"), codec));
    QVERIFY(!RouteParser::isFailureStatus(QByteArrayLiteral("8:1:x"), codec));
    QVERIFY(!RouteParser::isFailureStatus(QByteArrayLiteral("18:2:x"), codec));
}

void TestRoute::routePayloadBuilderMatchesFixture()
{
    // The mock server builds status-1 routes with the same field order as the
    // recovered fixture; this pins that builder to the reference layout.
    const QJsonObject fixture = uvtest::Fixtures::instance().object(QStringLiteral("route"));
    const QByteArray built = uvtest::routePayload(
        fixture.value(QStringLiteral("tcp_server_ip")).toString(),
        static_cast<quint16>(fixture.value(QStringLiteral("tcp_port")).toInt()),
        fixture.value(QStringLiteral("public_ip")).toString(),
        static_cast<quint16>(fixture.value(QStringLiteral("public_port")).toInt()),
        fixture.value(QStringLiteral("tcp_server_hostname")).toString(),
        fixture.value(QStringLiteral("partner_secret_token")).toString());
    const TargetRoute route = RouteParser::parseRoute(built, AnsiCodec::cp1252());
    QCOMPARE(route.tcpServerIp, fixture.value(QStringLiteral("tcp_server_ip")).toString());
    QCOMPARE(route.publicIp, fixture.value(QStringLiteral("public_ip")).toString());
    QCOMPARE(route.tcpPort, static_cast<quint16>(fixture.value(QStringLiteral("tcp_port")).toInt()));
    QCOMPARE(route.tcpServerHostname, fixture.value(QStringLiteral("tcp_server_hostname")).toString());
    QCOMPARE(route.partnerSecretToken.value_or(QString()),
             fixture.value(QStringLiteral("partner_secret_token")).toString());
}

void TestRoute::statusMessages()
{
    // The three documented statuses carry these exact meanings, and no message may
    // leak credentials or raw payload bytes.
    QCOMPARE(RouteParser::authenticationError(QStringLiteral("2")).info().message,
             QStringLiteral("wrong password"));
    QCOMPARE(RouteParser::authenticationError(QStringLiteral("11")).info().message,
             QStringLiteral("partner is offline or the ID does not exist"));
    QCOMPARE(RouteParser::authenticationError(QStringLiteral("99")).code(), ErrorCode::Network);
    const UvException unknown = RouteParser::authenticationError(QStringLiteral("7"));
    QCOMPARE(unknown.code(), ErrorCode::Authentication);
    QVERIFY(unknown.info().summary().contains(QStringLiteral("7")));
}

QTEST_MAIN(TestRoute)
#include "tst_route.moc"
