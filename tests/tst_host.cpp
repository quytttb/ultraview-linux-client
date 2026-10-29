//
// Host-mode protocol and state-machine tests.
//
// The host wire representation is pinned by the MacViewer/LinuxViewer reference
// (MIT) rather than the controller fixtures; these tests exercise the security
// boundaries around login, approval, pacing and input as well.
//
#include "host/HostBackend.h"
#include "host/HostProfile.h"
#include "host/HostProtocol.h"
#include "host/HostSession.h"
#include "host/InputMapping.h"

#include "core/ErrorTypes.h"
#include "protocol/Digest.h"
#include "protocol/PacketCodec.h"

#include <QtEndian>
#include <QtTest>

#include <zstd.h>

using namespace ultraview;
using namespace ultraview::host;

namespace {

HostIdentity identity()
{
    HostIdentity value;
    value.deviceIdentity = QStringLiteral("A1B2C3D4E5F60718293A4B5C6D7E8F90");
    value.password = QStringLiteral("12345678");
    value.displayName = QStringLiteral("Test host");
    value.localAddress = QStringLiteral("192.0.2.10");
    value.macAddress = QStringLiteral("001122334455");
    value.operatingSystem = QStringLiteral("0|6|0|8|0|0");
    value.userName = QStringLiteral("tester");
    return value;
}

HostFrame frame(uchar value, int width = 2, int height = 2)
{
    HostFrame result;
    result.width = width;
    result.height = height;
    result.rgb555BottomUp = QByteArray(width * height * 2, static_cast<char>(value));
    result.layoutId = QStringLiteral("one");
    result.monitors = {};
    return result;
}

QByteArray loginPayload(const QString &password, const QString &colors = QStringLiteral("2"))
{
    const QString nonce = QStringLiteral("12345678");
    const QString proof = protocol::md5HexLower((nonce + password).toUtf8());
    return QStringLiteral("login:%1:%2:1920:1080:7:15:15:30:16:111222333:Windows "
                          "viewer:1:%3:x:conn:%4:45:0:0::6.6.133:0")
        .arg(nonce, proof, QString(32, QLatin1Char('0')), colors)
        .toUtf8();
}

QByteArray utf16le(const QString &text)
{
    QByteArray out;
    for (const QChar unit : text) {
        out.append(static_cast<char>(unit.unicode() & 0xff));
        out.append(static_cast<char>((unit.unicode() >> 8) & 0xff));
    }
    return out;
}

QList<HostAction> peerActions(const QList<HostAction> &actions)
{
    QList<HostAction> result;
    for (const HostAction &action : actions) {
        if (action.type == HostAction::Type::SendPeer)
            result.append(action);
    }
    return result;
}

} // namespace

class TestHost : public QObject {
    Q_OBJECT

private slots:
    void registrationTerms();
    void registrationReply();
    void relayNegotiation();
    void peerPayloadDecoding();
    void loginVerification();
    void loginThrottle();
    void inputDecoding();
    void screenInitialization();
    void fullFrameEncoding();
    void rgb555Conversion();
    void sessionApprovalAndPacing();
    void sessionWrongPassword();
    void sessionTimeoutAndDisconnect();
    void profileHelpers();
    void layoutScaling();
    void inputMapping();
};

void TestHost::registrationTerms()
{
    const QStringList terms = identity().registrationTerms();
    QCOMPARE(terms.size(), 28);
    QCOMPARE(terms.at(0), QStringLiteral("3"));
    QCOMPARE(terms.at(1), identity().deviceIdentity);
    QCOMPARE(terms.at(2), identity().password);
    QCOMPARE(terms.at(4), identity().localAddress);
    QCOMPARE(terms.at(6), QStringLiteral("VGVzdCBob3N0"));
    QCOMPARE(terms.at(7), QStringLiteral("6.6.133"));
    QCOMPARE(terms.at(8), identity().macAddress);
    QCOMPARE(terms.at(10), identity().operatingSystem);
    QCOMPARE(terms.at(13), QString());
    QCOMPARE(terms.at(16), identity().userName);
    const QByteArray command = buildRegistrationCommand(identity());
    QCOMPARE(command.count(':'), 27);
    QVERIFY(command.startsWith("3:A1B2"));
}

void TestHost::registrationReply()
{
    const RegistrationReply reply = parseRegistrationReply(
        QByteArrayLiteral("3:443:00111222333:203.0.113.7:unknown:1:ping-list:0:17/10/2026:"
                          "relay.ultraviewer.net:opaque-token:future:"));
    QCOMPARE(reply.publicPort, static_cast<quint16>(443));
    QCOMPARE(reply.id, QStringLiteral("111222333"));
    QCOMPARE(reply.publicIp, QStringLiteral("203.0.113.7"));
    QCOMPARE(reply.status, 0);
    QCOMPARE(reply.pingList, QStringLiteral("ping-list"));
    QCOMPARE(reply.pingHostNames, QStringLiteral("relay.ultraviewer.net"));
    QCOMPARE(reply.serverToken, QStringLiteral("opaque-token"));

    QVERIFY_EXCEPTION_THROWN(parseRegistrationReply(QByteArrayLiteral("3:443:123:192.0.2.1")),
                             UvException);
    QVERIFY_EXCEPTION_THROWN(parseRegistrationReply(QByteArrayLiteral("3:0:1:192.0.2.1::::::")),
                             UvException);
    QVERIFY_EXCEPTION_THROWN(parseRegistrationReply(QByteArrayLiteral("3:443:1:999.0.0.1::::::")),
                             UvException);
}

void TestHost::relayNegotiation()
{
    const auto direct = parseRelayRequest(QByteArrayLiteral("11:203.0.113.5:4000:relay.ultraviewer.net"),
                                          {}, {});
    QVERIFY(direct.has_value());
    QCOMPARE(direct->address, QStringLiteral("203.0.113.5"));
    QCOMPARE(direct->hostname, QStringLiteral("relay.ultraviewer.net"));

    const auto request = parseRelayRequest(
        QByteArrayLiteral("17:tag1:hash1::relay.ultraviewer.net:0:1:1:0"),
        QStringLiteral("203.0.113.5;"), {});
    QVERIFY(request.has_value());
    QCOMPARE(request->address, QStringLiteral("203.0.113.5"));
    QCOMPARE(request->tag, QStringLiteral("tag1"));
    QCOMPARE(request->hash, QStringLiteral("hash1"));
    QVERIFY(!parseRelayRequest(QByteArrayLiteral("11:203.0.113.5:4000:evil.example"),
                               {}, {}).has_value());
    QVERIFY(!parseRelayRequest(QByteArrayLiteral("17:tag:hash:::relay.ultraviewer.net:2:1:1:0"),
                               QStringLiteral("203.0.113.5;"), {}).has_value());

    const auto address = parseRelayAddress(QByteArrayLiteral("0:4000:203.0.113.5"));
    QVERIFY(address.has_value());
    QCOMPARE(address->port, static_cast<quint16>(4000));
    QCOMPARE(address->address, QStringLiteral("203.0.113.5"));
    QCOMPARE(relayAvailability(*request, *address),
             QByteArrayLiteral("17:tag1:hash1:203.0.113.5:4000:203.0.113.5:0:|0"));
}

void TestHost::peerPayloadDecoding()
{
    QCOMPARE(peerPayload(QByteArrayLiteral("17"), false), std::optional<QByteArray>("17"));
    QCOMPARE(peerPayload(utf16le(QStringLiteral("17")), true),
             std::optional<QByteArray>("17"));
    QVERIFY(!peerPayload(QByteArrayLiteral("1"), true).has_value());
    QVERIFY(peerPayload(utf16le(QStringLiteral("1\n7")), true).has_value());
    QVERIFY(!peerPayload(utf16le(QStringLiteral("1") + QChar(1)), true).has_value());
}

void TestHost::loginVerification()
{
    const auto login = parseLogin(loginPayload(QStringLiteral("test-pass")));
    QVERIFY(login.has_value());
    QCOMPARE(login->claimedId(), QStringLiteral("111222333"));
    QCOMPARE(login->displayName(), QStringLiteral("Windows viewer"));
    QCOMPARE(login->screenWidth, 1920);
    QCOMPARE(login->controlMode, 1);
    QCOMPARE(login->displayColors, 2);
    QCOMPARE(login->displayQuality, 45);
    QVERIFY(verifyRandomPassword(*login, QStringLiteral("test-pass")));
    QVERIFY(!verifyRandomPassword(*login, QStringLiteral("wrong")));

    QVERIFY(!parseLogin(QByteArrayLiteral("login:bad")).has_value());
    QVERIFY(!parseLogin(loginPayload(QStringLiteral("test-pass")).replace("1920", "0"))
                 .has_value());
    QVERIFY(parseLogin(loginPayload(QStringLiteral("test-pass"), QStringLiteral("1"))).has_value());
}

void TestHost::loginThrottle()
{
    LoginThrottle throttle;
    const qint64 now = 1000;
    for (int attempt = 0; attempt < 4; ++attempt)
        QVERIFY(!throttle.recordFailure(now));
    QVERIFY(throttle.recordFailure(now));
    QVERIFY(throttle.isLocked(now));
    QVERIFY(throttle.isLocked(now + LoginThrottle::kLockoutMs - 1));
    QVERIFY(!throttle.isLocked(now + LoginThrottle::kLockoutMs));
    QVERIFY(!throttle.recordFailure(now + LoginThrottle::kLockoutMs));
    QVERIFY(throttle.failures() == 1);
    throttle.recordSuccess();
    QCOMPARE(throttle.failures(), 0u);
}

void TestHost::inputDecoding()
{
    const auto move = decodeInput(QByteArrayLiteral("8|||512|||0|||10|||20"));
    QVERIFY(move.has_value());
    QVERIFY(move->type == HostInputType::Mouse);
    QCOMPARE(move->message, 0x200u);
    QCOMPARE(move->x, 10);
    QCOMPARE(move->y, 20);

    const auto leftDown = decodeInput(QByteArrayLiteral("8|||513|||1|||655370"));
    QVERIFY(leftDown.has_value());
    QCOMPARE(leftDown->x, 10);
    QCOMPARE(leftDown->y, 10);

    const auto wheel = decodeInput(QByteArrayLiteral("8|||522|||1966080|||0"));
    QVERIFY(wheel.has_value());
    QCOMPARE(wheel->wheel, static_cast<qint16>(30));

    QByteArray key(11, '\0');
    key[0] = 30;
    key[1] = '|';
    key[2] = '|';
    key[3] = '|';
    key[4] = 1;
    key[5] = 65;
    key[6] = 0x1e;
    const auto keyboard = decodeInput(key);
    QVERIFY(keyboard.has_value());
    QVERIFY(keyboard->type == HostInputType::Keyboard);
    QCOMPARE(keyboard->kind, static_cast<quint8>(1));
    QCOMPARE(keyboard->virtualKey, static_cast<quint8>(65));
    QCOMPARE(keyboard->scanCode, static_cast<quint16>(0x1e));

    QVERIFY(!decodeInput(QByteArrayLiteral("8|||999|||0|||1|||2")).has_value());
    QVERIFY(!decodeInput(QByteArrayLiteral("8|||512|||0|||1")).has_value());
}

void TestHost::screenInitialization()
{
    const QByteArray payload = initialization(1920, 1080, QStringLiteral("6.6.133"), {});
    const QList<QByteArray> fields = protocol::decodeByteArrayFields(payload, 11);
    QCOMPARE(fields.at(0), QByteArray(1, '\0'));
    QCOMPARE(fields.at(6), QByteArrayLiteral("6.6.133"));
    QCOMPARE(fields.at(8), QByteArrayLiteral("0:0:1920:1080|"));
    QCOMPARE(fields.at(9), QByteArray(4, '\0'));
    QCOMPARE(fields.at(10), QByteArray(4, '\0'));

    const QByteArray monitor = initialization(2, 2, QStringLiteral("6.6.133"),
                                              {HostMonitor{0, 0, 1, 2}, HostMonitor{1, 0, 2, 2}});
    QCOMPARE(protocol::decodeByteArrayFields(monitor, 11).at(8),
             QByteArrayLiteral("0:0:1:2|1:0:2:2|"));
}

void TestHost::fullFrameEncoding()
{
    const HostFrame source = frame('\x11');
    const QByteArray payload = fullFrame(source, kFirstFrameNumber);
    const QList<QByteArray> fields = protocol::decodeByteArrayFields(payload, 11);
    QCOMPARE(fields.at(0), QByteArray(1, '\x01'));
    QCOMPARE(fields.at(3), QByteArray());
    QCOMPARE(fields.at(4).size(), 4);
    QCOMPARE(static_cast<qint32>(qFromLittleEndian<qint32>(
                 reinterpret_cast<const uchar *>(fields.at(4).constData()))),
             kFirstFrameNumber);

    QByteArray topDown(8, '\x11');
    QByteArray decoded(static_cast<int>(topDown.size()), '\0');
    const size_t produced = ZSTD_decompress(decoded.data(), static_cast<size_t>(decoded.size()),
                                            fields.at(2).constData(),
                                            static_cast<size_t>(fields.at(2).size()));
    QVERIFY(!ZSTD_isError(produced));
    QCOMPARE(static_cast<int>(produced), static_cast<int>(topDown.size()));
    QCOMPARE(decoded, topDown);
}

void TestHost::rgb555Conversion()
{
    uchar pixels[2 * 2 * 4] = {
        0, 0, 255, 0,   0, 255, 0, 0,
        255, 0, 0, 0,   0, 0, 0, 0,
    };
    const QByteArray converted = rgb555FromBgrx(pixels, 2, 2, 8, false);
    QCOMPARE(converted.size(), 8);
    // Bottom-up: the second source row comes first.
    QCOMPARE(static_cast<uchar>(converted.at(0)), static_cast<uchar>(0x1f));
    QCOMPARE(static_cast<uchar>(converted.at(1)), static_cast<uchar>(0x00));
    QCOMPARE(static_cast<uchar>(converted.at(2)), static_cast<uchar>(0x00));
    QCOMPARE(static_cast<uchar>(converted.at(3)), static_cast<uchar>(0x00));
    // Top row, left red pixel: RGB555 red is 0x7c00.
    QCOMPARE(static_cast<uchar>(converted.at(4)), static_cast<uchar>(0x00));
    QCOMPARE(static_cast<uchar>(converted.at(5)), static_cast<uchar>(0x7c));
    QCOMPARE(scaleRgb555(converted, 2, 2, 1, 1).size(), 2);
}

void TestHost::sessionApprovalAndPacing()
{
    HostSession session(QStringLiteral("test-pass"));
    const QString address = QStringLiteral("203.0.113.5");
    constexpr quint16 port = 4000;
    const qint64 now = 1000;
    session.processPeer(loginPayload(QStringLiteral("test-pass")), address, port, now);
    QVERIFY(session.awaitingApproval());
    QList<HostAction> actions = session.takeActions();
    QCOMPARE(actions.size(), 1);
    QVERIFY(actions.at(0).type == HostAction::Type::Prompt);
    QCOMPARE(actions.at(0).viewerId, QStringLiteral("111222333"));

    QString error;
    QVERIFY(session.approve(frame('\x01'), false, now, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    actions = session.takeActions();
    QCOMPARE(peerActions(actions).size(), 2);
    const auto init = protocol::decodeByteArrayFields(peerActions(actions).at(0).payload, 11);
    QCOMPARE(init.at(0), QByteArray(1, '\0'));
    QCOMPARE(init.at(1), QByteArray::fromHex("02000000"));
    QCOMPARE(peerActions(actions).at(1).payload,
             controlPermission(false));

    session.processPeer(QByteArrayLiteral("17"), address, port, now);
    actions = session.takeActions();
    QVERIFY(session.streaming());
    QVERIFY(actions.at(0).type == HostAction::Type::Connected);
    QCOMPARE(peerActions(actions).size(), 1);
    const auto first = protocol::decodeByteArrayFields(peerActions(actions).at(0).payload, 11);
    QCOMPARE(first.at(0), QByteArray(1, '\x01'));
    QCOMPARE(static_cast<qint32>(qFromLittleEndian<qint32>(
                 reinterpret_cast<const uchar *>(first.at(4).constData()))),
             kFirstFrameNumber);
    QVERIFY(!session.takeCaptureDue(now + 1000));

    session.processPeer(QByteArrayLiteral("18"), address, port, now);
    QVERIFY(!session.takeCaptureDue(now));
    QVERIFY(session.takeCaptureDue(now + 40));
    QVERIFY(session.submitFrame(frame('\x01'), now + 40, &error));
    QVERIFY(peerActions(session.takeActions()).isEmpty());
    QVERIFY(session.takeCaptureDue(now + 40 + 250));
    QVERIFY(session.submitFrame(frame('\x02'), now + 40 + 250, &error));
    const auto second = protocol::decodeByteArrayFields(
        peerActions(session.takeActions()).at(0).payload, 11);
    QCOMPARE(static_cast<qint32>(qFromLittleEndian<qint32>(
                 reinterpret_cast<const uchar *>(second.at(4).constData()))),
             kFirstFrameNumber + 1);

    session.setControlAllowed(true);
    actions = session.takeActions();
    QCOMPARE(peerActions(actions).at(0).payload, controlPermission(true));
    session.processPeer(QByteArrayLiteral("8|||512|||0|||10|||20"), address, port, now);
    actions = session.takeActions();
    QCOMPARE(actions.size(), 1);
    QVERIFY(actions.at(0).type == HostAction::Type::Input);
    QCOMPARE(actions.at(0).input.x, 10);
}

void TestHost::sessionWrongPassword()
{
    HostSession session(QStringLiteral("test-pass"));
    const QString address = QStringLiteral("203.0.113.5");
    for (int attempt = 0; attempt < 5; ++attempt) {
        session.processPeer(loginPayload(QStringLiteral("wrong")), address, 4000, 1000);
        const auto actions = peerActions(session.takeActions());
        QCOMPARE(actions.size(), 1);
        QCOMPARE(actions.at(0).payload, singleCommand(22));
    }
    session.processPeer(loginPayload(QStringLiteral("test-pass")), address, 4000, 1000);
    QCOMPARE(peerActions(session.takeActions()).at(0).payload, singleCommand(22));
    QVERIFY(!session.hasViewer());
}

void TestHost::sessionTimeoutAndDisconnect()
{
    HostSession session(QStringLiteral("test-pass"));
    const QString address = QStringLiteral("203.0.113.5");
    session.processPeer(loginPayload(QStringLiteral("test-pass")), address, 4000, 1000);
    session.takeActions();
    session.tick(1000 + 45 * 1000);
    QVERIFY(!session.hasViewer());
    QList<HostAction> actions = session.takeActions();
    bool ended = false;
    for (const HostAction &action : actions)
        ended = ended || action.type == HostAction::Type::Ended;
    QVERIFY(ended);

    session.processPeer(loginPayload(QStringLiteral("test-pass")), address, 4000, 2000);
    session.takeActions();
    QString error;
    QVERIFY(session.approve(frame('\x01'), true, 2000, &error));
    session.takeActions();
    session.processPeer(QByteArrayLiteral("17"), address, 4000, 2000);
    session.takeActions();
    session.processPeer(QByteArrayLiteral("19"), address, 4000, 2000);
    QVERIFY(!session.hasViewer());
    actions = peerActions(session.takeActions());
    QCOMPARE(actions.size(), 1);
    QCOMPARE(actions.at(0).payload, singleCommand(27));
}

void TestHost::profileHelpers()
{
    QCOMPARE(HostProfile::operatingSystemWireValue(QStringLiteral("6.17.0-14-generic")),
             QStringLiteral("0|6|0|17|0|0"));
    QCOMPARE(HostProfile::normalizeMac(QStringLiteral("00:11:22:33:44:55")),
             QStringLiteral("001122334455"));
    QVERIFY(HostProfile::normalizeMac(QStringLiteral("01:11:22:33:44:55")).isEmpty());
    QVERIFY(HostProfile::normalizeMac(QStringLiteral("00:00:00:00:00:00")).isEmpty());
    const QString fallback = HostProfile::fallbackMac(identity().deviceIdentity);
    QCOMPARE(fallback.size(), 12);
    QCOMPARE(fallback.left(2).toInt(nullptr, 16) & 0x03, 0x02);
    QVERIFY(HostProfile::isAcceptablePassword(QStringLiteral("12345678")));
    QVERIFY(!HostProfile::isAcceptablePassword(QStringLiteral("1234")));
    QVERIFY(!HostProfile::isAcceptablePassword(QStringLiteral("1234\t567")));
}

void TestHost::layoutScaling()
{
    QString error;
    const auto single = HostLayout::build({HostRect{0, 0, 1920, 1080}}, &error);
    QVERIFY2(single.has_value(), qPrintable(error));
    QCOMPARE(single->width, 1920);
    QCOMPARE(single->height, 1080);
    QCOMPARE(single->mapToSource(0, 0), QPoint(0, 0));
    QCOMPARE(single->mapToSource(1919, 1079), QPoint(1919, 1079));
    QCOMPARE(single->mapToSource(-5, 99999), QPoint(0, 1079));

    const auto scaled = HostLayout::build({HostRect{0, 0, 3840, 2160}}, &error);
    QVERIFY2(scaled.has_value(), qPrintable(error));
    QCOMPARE(scaled->width, 1920);
    QCOMPARE(scaled->height, 1080);
    QCOMPARE(scaled->mapToSource(1919, 1079), QPoint(3838, 2158));

    const auto dual = HostLayout::build(
        {HostRect{0, 0, 1920, 1080}, HostRect{1920, 0, 1280, 1024}}, &error);
    QVERIFY2(dual.has_value(), qPrintable(error));
    QCOMPARE(dual->width, 3200);
    QCOMPARE(dual->height, 1080);
    QCOMPARE(dual->monitors.size(), 2);
    QCOMPARE(dual->mapToSource(1920, 0), QPoint(1920, 0));

    QVERIFY(!HostLayout::build({}, &error).has_value());
    QVERIFY(!HostLayout::build({HostRect{0, 0, 0, 1080}}, &error).has_value());
}

void TestHost::inputMapping()
{
    const auto letter = mapKey(1, 0x41, 0x1e, 0);
    QVERIFY(letter.has_value());
    QVERIFY(letter->kind == PortalKey::Kind::Keysym);
    QCOMPARE(letter->value, 0x61);

    const auto function = mapKey(1, 0x74, 0x3f, 0);
    QVERIFY(function.has_value());
    QCOMPARE(function->value, 0xffc2);

    const auto unicode = mapKey(1, kVirtualKeyPacket, 0, 0x20ac);
    QVERIFY(unicode.has_value());
    QCOMPARE(unicode->value, 0x010020ac);
    QVERIFY(!mapKey(1, kVirtualKeyPacket, 0, 0x01).has_value());

    const auto extendedEnter = mapKey(1, 0x0d, 0xe01c, 0);
    QVERIFY(extendedEnter.has_value());
    QCOMPARE(extendedEnter->value, 0xff8d);

    // Unknown virtual key, known extended scan code: fall back to evdev.
    const auto evdev = mapKey(1, 0xff, 0xe048, 0);
    QVERIFY(evdev.has_value());
    QVERIFY(evdev->kind == PortalKey::Kind::Keycode);
    QCOMPARE(evdev->value, 103);
    QVERIFY(!mapKey(1, 0xff, 0x7f, 0).has_value());

    qint32 remainder = 0;
    HostInputEvent move;
    move.message = kMouseMoveMessage;
    move.x = 10;
    move.y = 20;
    const PointerAction pointer = pointerAction(move, &remainder);
    QVERIFY(pointer.kind == PointerAction::Kind::Move);
    QCOMPARE(pointer.x, 10);
    QCOMPARE(pointer.y, 20);

    HostInputEvent wheel;
    wheel.message = kMouseWheelMessage;
    wheel.wheel = 60;
    QVERIFY(pointerAction(wheel, &remainder).kind == PointerAction::Kind::None);
    wheel.wheel = 60;
    const PointerAction notch = pointerAction(wheel, &remainder);
    QVERIFY(notch.kind == PointerAction::Kind::Wheel);
    QCOMPARE(notch.steps, 1);
    QCOMPARE(remainder, 0);

    HostInputEvent leftDown;
    leftDown.message = 0x201;
    const PointerAction button = pointerAction(leftDown, &remainder);
    QVERIFY(button.kind == PointerAction::Kind::Button);
    QCOMPARE(button.button, static_cast<quint8>(1));
    QVERIFY(button.pressed);
}

QTEST_GUILESS_MAIN(TestHost)
#include "tst_host.moc"
