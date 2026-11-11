//
// End-to-end session tests against the offline mock server in TestSupport.cpp.
//
// These drive the real production path: `SessionManager` (worker thread) ->
// `ConnectionManager` -> `RegistrationTransport`/`RelayTransport` over TLS ->
// `ScreenDecoder` -> `FrameUpdate` patches, with the two channels of the mock
// server standing in for the Windows partner.  No real UltraViewer server is
// contacted: the mock speaks the recovered wire format (fixtures pinned) over
// 127.0.0.1 with the test certificate.
//
#include "TestSupport.h"

#include "core/ErrorTypes.h"
#include "core/SessionManager.h"
#include "core/SessionState.h"
#include "decoder/FrameBuffer.h"
#include "host/HostProfile.h"
#include "models/ConnectionViewModel.h"
#include "protocol/AuthenticationCodec.h"
#include "protocol/Digest.h"
#include "protocol/InputCodec.h"
#include "protocol/RegistrationCodec.h"
#include "protocol/RouteParser.h"
#include "security/TlsPolicy.h"

#include <QImage>
#include <QFile>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace ultraview;
using namespace ultraview::protocol;
using namespace ultraview::network;

namespace {

/// Records everything a test needs from one session run.
struct SessionRecorder {
    QList<ErrorInfo> failures;
    QList<ErrorInfo> notices;
    QList<QString> diagnostics;
    QList<QSize> desktopSizes;
    QList<protocol::TargetRoute> routes;
    QList<decoder::FrameUpdatePtr> frames;
    QList<SessionState> states;
    int stopped = 0;
    QImage mirror;

    void attach(SessionManager *session)
    {
        QObject::connect(session, &SessionManager::failed, [this](const ErrorInfo &error) {
            failures.append(error);
        });
        QObject::connect(session, &SessionManager::notice, [this](const ErrorInfo &note) {
            notices.append(note);
        });
        QObject::connect(session, &SessionManager::diagnostic, [this](const QString &message) {
            diagnostics.append(message);
        });
        QObject::connect(session, &SessionManager::connected, [this](const QSize &size) {
            desktopSizes.append(size);
        });
        QObject::connect(session, &SessionManager::routeResolved, [this](const protocol::TargetRoute &route) {
            routes.append(route);
        });
        QObject::connect(session, &SessionManager::frameReady, [this](const decoder::FrameUpdatePtr &update) {
            frames.append(update);
            if (mirror.size() != update->canvasSize) {
                mirror = QImage(update->canvasSize, QImage::Format_RGB32);
                mirror.fill(Qt::black);
            }
            for (int row = 0; row < update->rect.height(); ++row) {
                std::memcpy(mirror.scanLine(update->rect.top() + row) + qsizetype(update->rect.left()) * 4,
                            update->patch.constScanLine(row), size_t(update->rect.width()) * 4);
            }
        });
        QObject::connect(session, &SessionManager::stateChanged, [this](SessionState state) {
            states.append(state);
        });
        QObject::connect(session, &SessionManager::stopped, [this] { ++stopped; });
    }

    bool hasFailure(ErrorCode code) const
    {
        for (const ErrorInfo &error : failures) {
            if (error.code == code)
                return true;
        }
        return false;
    }

    bool hasNotice(ErrorCode code) const
    {
        for (const ErrorInfo &note : notices) {
            if (note.code == code)
                return true;
        }
        return false;
    }

    QByteArray mirrorRgb888() const
    {
        if (mirror.isNull())
            return {};
        const QImage rgb888 = mirror.convertToFormat(QImage::Format_RGB888);
        const qsizetype rowBytes = qsizetype{rgb888.width()} * 3;
        QByteArray out;
        for (int row = 0; row < rgb888.height(); ++row)
            out.append(reinterpret_cast<const char *>(rgb888.constScanLine(row)), rowBytes);
        return out;
    }
};

security::TlsPolicy testPolicy()
{
    security::TlsPolicy policy;
    policy.setAdditionalCaCertificateFile(uvtest::Fixtures::instance().tlsCertificatePath());
    return policy;
}

SessionRequest makeRequest(const uvtest::MockUltraviewServer &server)
{
    SessionRequest request;
    request.registrationTerms = uvtest::testRegistrationTerms();
    request.requestTerms = uvtest::testRequestTerms();
    request.password = QStringLiteral("test-password");
    request.passwordType = 0;
    request.partnerId = QStringLiteral("123456789");
    request.hostName = QStringLiteral("linux-tester");
    request.viewportHint = QSize(1280, 720);
    request.tlsPolicy = testPolicy();
    request.timeoutMs = 700;
    request.screenDictionary = uvtest::Fixtures::instance().dictionary();
    // Everything points at the local mock server; the recovered production
    // endpoints are never contacted by this suite.
    request.registrationHost = QStringLiteral("127.0.0.1");
    request.registrationPort = server.port();
    request.registrationHttpHost = QStringLiteral("functions3.ultraviewer.test");
    request.registrationPeerName = QStringLiteral("ultraviewer");
    request.relayHostOverride = QStringLiteral("127.0.0.1");
    request.relayPortOverride = server.port();
    request.relayHttpHostOverride = QStringLiteral("relay.ultraviewer.test");
    return request;
}

uvtest::MockUltraviewServer::Options mockOptions()
{
    uvtest::MockUltraviewServer::Options options;
    options.desktopSize = QSize(64, 48);
    options.routeTcpServerIp = QStringLiteral("127.0.0.1");
    options.routeTcpPort = 4433;
    options.routePublicIp = QStringLiteral("127.0.0.1");
    options.routePublicPort = 50000;
    options.routeHostname = QStringLiteral("relay.ultraviewer.test");
    options.routeSecretToken = QStringLiteral("test-token");
    return options;
}

/// The first `screen_sequences` entry: two RGB555 rectangles in one zstd stream.
QJsonObject rgb555Sequence()
{
    return uvtest::Fixtures::instance().array(QStringLiteral("screen_sequences")).at(0).toObject();
}

} // namespace

class TestSession : public QObject {
    Q_OBJECT

private slots:
    void happyPathEndToEnd();
    void automaticRegistrationValidation();
    void refusalStatusCodes_data();
    void refusalStatusCodes();
    void registrationTimeout();
    void registrationCaptchaRetry_data();
    void registrationCaptchaRetry();
    void registrationCaptchaTimeoutAndCancel();
    void registrationCaptchaInvalidAndRetryLimit();
    void unrelatedRegistrationPayloadIsIgnored();
    void selectedRegistrationProfileIsUsed();
    void relayLoginTimeout();
    void relayClosedByPartner();
    void audioAndUnknownKindsAreRefusedButKeepTheSession();
    void bitBltUpdateIsRefusedFatally();
    void kind3SizeFollowUp();
    void coalescedFramesFlushWhenRendererCatchesUp();
    void reconnectStartsWithFreshState();
    void disconnectNotifiesPartner();
    void reconnectReceivesScreenUpdates();
    void registrationChannelLossDoesNotBreakTheSession();
    void inputCommandsReachThePartner();
    void invalidRequestIsRejectedLocally();
    void certificateNameMismatchIsRefused();
    void registrationTlsAddressFallback();
    void registrationTlsAddressFallbackFailsClosed();
    void registrationTlsAddressFallbackCanBeCancelled();
    void registrationDnsAddressFallback();
    void optionalLiveTlsFallback();
    void defaultTlsNamesUseTheEndpoint();
    void tlsPolicyAddsServiceChainCertificates();
    void relayAuthenticatesTheRegistrationCertificateName();
    void relayCertificateNameOverrideIsRefusedWhenItDoesNotMatch();
    void acceptAnyCertificateIsExplicit();
};

void TestSession::automaticRegistrationValidation()
{
    uvtest::MockUltraviewServer server;
    SessionRequest request = makeRequest(server);
    host::HostIdentity identity;
    identity.deviceIdentity = QString(32, QLatin1Char('A'));
    identity.password = QStringLiteral("12345678");
    identity.displayName = QStringLiteral("test");
    identity.localAddress = QStringLiteral("127.0.0.1");
    identity.macAddress = QStringLiteral("001122334455");
    identity.operatingSystem = QStringLiteral("0|0|0|0|0|0");
    identity.userName = QStringLiteral("test");
    request.registrationTerms = identity.registrationTerms();
    QString error;
    QVERIFY2(request.validate(&error), qPrintable(error));
    request.registrationTerms[4] = QStringLiteral("invalid:field");
    QVERIFY(!request.validate(&error));
}

void TestSession::happyPathEndToEnd()
{
    uvtest::MockUltraviewServer server;
    server.setOptions(mockOptions());
    QVERIFY(server.listen());
    server.setOptions([&] {
        auto options = mockOptions();
        options.relayPackets = {uvtest::Fixtures::entryBytes(rgb555Sequence()
                                                                 .value(QStringLiteral("updates"))
                                                                 .toArray()
                                                                 .at(0)
                                                                 .toObject(),
                                                             QStringLiteral("packet_hex"))};
        return options;
    }());
    QVERIFY(server.options().relayPackets.size() == 1);

    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    connect(&session, &SessionManager::frameReady, &session,
            [&session] { session.releaseFrames(1); });

    const SessionRequest request = makeRequest(server);
    session.start(request);

    // 1. registration: command 3 -> route
    QVERIFY(server.waitFor([&] { return server.routeSent(); }));
    // The mock reports the route as soon as it is written; the session reaches
    // RelayConnecting when it has processed the reply.
    QVERIFY(server.waitFor([&] { return session.state() == SessionState::RelayConnecting; }));
    QCOMPARE(session.state(), SessionState::RelayConnecting);
    QVERIFY(server.registrationCommand().startsWith("3:"));
    QCOMPARE(server.registrationCommand(),
             RegistrationCodec::buildRegistrationCommand(request.registrationTerms, AnsiCodec::cp1252()));
    QVERIFY(server.connectionRequest().startsWith("8:"));
    QCOMPARE(server.connectionRequest().count(':') + 1, 17);

    // 2. relay: login -> kind 0 -> updates
    QVERIFY(server.waitFor([&] { return server.relayLoginDone(); }));
    // The mock records the login when the frame arrives; the session is Connected
    // only after the kind-0 acknowledgement has been processed.
    QVERIFY(server.waitFor([&] { return session.state() == SessionState::Connected; }, 3000));
    QCOMPARE(session.state(), SessionState::Connected);
    QCOMPARE(session.desktopSize(), QSize(64, 48));
    QCOMPARE(recorder.desktopSizes.value(0), QSize(64, 48));
    QCOMPARE(recorder.routes.size(), 1);
    QCOMPARE(recorder.routes.first().tcpServerIp, QStringLiteral("127.0.0.1"));
    QCOMPARE(recorder.routes.first().partnerSecretToken.value_or(QString()), QStringLiteral("test-token"));

    // The login payload is UTF-16LE (`login:` + 22 ':'-joined fields, flag 1).
    // Its exact digest semantics are the unit-test contract of
    // `AuthenticationCodec`; here the session-level contract is asserted
    // structurally: the schema is intact and the payload was derived from the
    // requested password, (random) salt, client id and partner token.
    const QByteArray loginPayload = server.loginPayload();
    QVERIFY(loginPayload.startsWith(utf16leBytes(QStringLiteral("login:"))));
    QVERIFY(loginPayload.size() % 2 == 0);
    QString loginText;
    loginText.resize(loginPayload.size() / 2);
    std::memcpy(loginText.data(), loginPayload.constData(), size_t(loginPayload.size()));
    QVERIFY(loginText.startsWith(QStringLiteral("login:")));
    const QStringList loginFields = loginText.mid(6).split(QLatin1Char(':'));
    QCOMPARE(loginFields.size(), 22);
    QCOMPARE(loginFields.at(2), QStringLiteral("1280"));
    QCOMPARE(loginFields.at(3), QStringLiteral("720"));
    QCOMPARE(loginFields.at(9), QStringLiteral("123456789"));
    QCOMPARE(loginFields.at(10), QStringLiteral("linux-tester"));
    QCOMPARE(loginFields.at(15), QStringLiteral("2"));
    QCOMPARE(loginFields.at(16), QStringLiteral("80"));
    QCOMPARE(loginFields.at(19), QStringLiteral("test-token"));
    QCOMPARE(loginFields.at(20), AuthenticationCodec::clientVersion());
    const auto isAsciiDigits = [](const QString &value) {
        for (const QChar unit : value) {
            if (unit < QLatin1Char('0') || unit > QLatin1Char('9'))
                return false;
        }
        return true;
    };
    // Positions 0, 1 and 12 are the password-derived values.
    const QString salt = loginFields.at(0);
    QCOMPARE(salt.size(), 8);
    QVERIFY(isAsciiDigits(salt));
    QCOMPARE(loginFields.at(1),
             AuthenticationCodec::loginNumericSaltDigest(request.password, salt, AnsiCodec::cp1252()));
    QCOMPARE(loginFields.at(12), AuthenticationCodec::connectionRequestPasswordValue(
                                     request.password, 0, AnsiCodec::cp1252()));
    QCOMPARE(loginFields.at(14).size(), 16);
    QVERIFY(isAsciiDigits(loginFields.at(14)));

    // The two channel prefaces are the recovered HTTP tunnel preface.
    QCOMPARE(server.prefaces().size(), 2);
    for (const QByteArray &preface : server.prefaces()) {
        QVERIFY(preface.startsWith("POST /UVService/ HTTP/1.1\r\n"));
        QVERIFY(preface.contains("Content-length: 8589934590\r\n"));
        QVERIFY(preface.endsWith("\r\n\r\n"));
    }
    QVERIFY(server.prefaces().at(0).contains("Host: functions3.ultraviewer.test\r\n"));
    QVERIFY(server.prefaces().at(1).contains("Host: relay.ultraviewer.test\r\n"));
    QCOMPARE(server.relayConnectionCount(), 1);

    // 3. the acknowledged screen update reaches the decoder as one patch set
    const QJsonObject update = rgb555Sequence().value(QStringLiteral("updates")).toArray().at(0).toObject();
    QVERIFY(server.waitFor([&] { return !recorder.frames.isEmpty(); }));
    QVERIFY(server.waitFor([&] { return recorder.frames.size() >= 2; }, 3000));
    // The first patch is the initial (black) canvas published with the login ack.
    QVERIFY(recorder.frames.first()->fullFrame);
    QCOMPARE(recorder.frames.first()->canvasSize, QSize(64, 48));
    QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(recorder.mirrorRgb888(),
                                                         QCryptographicHash::Md5)
                                    .toHex()),
             QString::fromLatin1(QCryptographicHash::hash(
                                     QByteArray::fromBase64(update.value(QStringLiteral("canvas_hex"))
                                                                .toString()
                                                                .toLatin1()),
                                     QCryptographicHash::Md5)
                                    .toHex()));

    // 4. acknowledgements on the relay channel: b"17" once, b"18" per update
    QVERIFY(server.waitForRelayPayload(QByteArrayLiteral("17")));
    QVERIFY(server.waitForRelayPayload(QByteArrayLiteral("18")));
    QCOMPARE(server.relayPayloads().first(), QByteArrayLiteral("17"));

    // 5. statistics snapshot
    // The snapshot is rate limited (see ConnectionManager::publishStatistics), so
    // wait for the coalescing timer to publish the counters of the decoded frames
    // before reading it.
    QTRY_VERIFY_WITH_TIMEOUT(session.statistics().framesPublished >= 2, 3000);
    const SessionStatistics statistics = session.statistics();
    QCOMPARE(statistics.clientId, 123456789);
    QCOMPARE(statistics.desktopSize, QSize(64, 48));
    QVERIFY(statistics.dictionaryInstalled);
    QVERIFY(statistics.loginAcknowledged);
    QVERIFY(statistics.updatesDecoded >= 1);
    QVERIFY(statistics.framesPublished >= 2);
    QVERIFY(statistics.relayBytesSent > 0);
    QVERIFY(statistics.relayBytesReceived > 0);
    QVERIFY(!statistics.relayPeer.isEmpty());
    QVERIFY(statistics.tlsWarning.isEmpty());

    // 6. teardown
    session.stop();
    QVERIFY(server.waitFor([&] { return recorder.stopped == 1; }));
    QCOMPARE(session.state(), SessionState::Disconnected);
    QVERIFY(recorder.failures.isEmpty());
    QVERIFY2(server.errors().isEmpty(), qPrintable(server.errors().join(QLatin1Char(';'))));
}

void TestSession::refusalStatusCodes_data()
{
    QTest::addColumn<QString>("status");
    QTest::addColumn<int>("code");
    QTest::newRow("wrong password") << QStringLiteral("2") << int(ErrorCode::Authentication);
    QTest::newRow("offline or unknown id") << QStringLiteral("11") << int(ErrorCode::Authentication);
    QTest::newRow("cannot connect") << QStringLiteral("99") << int(ErrorCode::Network);
    QTest::newRow("undocumented status") << QStringLiteral("7") << int(ErrorCode::Authentication);
}

void TestSession::refusalStatusCodes()
{
    QFETCH(QString, status);
    QFETCH(int, code);

    uvtest::MockUltraviewServer server;
    auto options = mockOptions();
    options.routeStatus = status.toLatin1();
    server.setOptions(options);
    QVERIFY(server.listen());

    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    session.start(makeRequest(server));

    QVERIFY(server.waitFor([&] { return !recorder.failures.isEmpty(); }));
    QCOMPARE(int(recorder.failures.first().code), code);
    QVERIFY(!recorder.failures.first().message.isEmpty());
    // A refused request never opens the relay channel.
    QCOMPARE(server.relayConnectionCount(), 0);
    QCOMPARE(session.state(), SessionState::Failed);
    // The user-visible message must not contain protocol bytes.
    QVERIFY(!recorder.failures.first().summary().contains(QStringLiteral("8:")));
}

void TestSession::registrationTimeout()
{
    uvtest::MockUltraviewServer server;
    auto options = mockOptions();
    options.muteRegistration = true;
    server.setOptions(options);
    QVERIFY(server.listen());

    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    session.start(makeRequest(server));

    QVERIFY(server.waitFor([&] { return server.registrationHandshakeDone(); }));
    QVERIFY(server.waitFor([&] { return !recorder.failures.isEmpty(); }, 5000));
    QCOMPARE(recorder.failures.first().code, ErrorCode::Timeout);
    QCOMPARE(server.relayConnectionCount(), 0);
    QCOMPARE(session.state(), SessionState::Failed);
}

void TestSession::registrationCaptchaRetry_data()
{
    QTest::addColumn<bool>("expanded");
    QTest::newRow("26-terms") << false;
    QTest::newRow("28-fields") << true;
}

void TestSession::registrationCaptchaRetry()
{
    QFETCH(bool, expanded);
    uvtest::MockUltraviewServer server;
    auto options = mockOptions();
    options.captchaChallenges = {QByteArrayLiteral("60:private-challenge:private-extra")};
    options.routeStatus = QByteArrayLiteral("11");
    server.setOptions(options);
    QVERIFY(server.listen());

    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    QSignalSpy captcha(&session, &SessionManager::captchaRequired);
    QSignalSpy cleared(&session, &SessionManager::captchaCleared);
    SessionRequest request = makeRequest(server);
    request.timeoutMs = 700;
    request.captchaTimeoutMs = 3000;
    request.captchaKeepAliveMs = 200;
    if (expanded) {
        const auto ticks = request.registrationTerms.takeAt(22).split(QLatin1Char(':'));
        for (int i = 0; i < ticks.size(); ++i)
            request.registrationTerms.insert(22 + i, ticks.at(i));
        QCOMPARE(request.registrationTerms.size(), 28);
    }
    session.start(request);
    QVERIFY(server.waitFor([&] { return captcha.size() == 1; }));
    const quint64 challengeId = captcha.first().at(0).toULongLong();
    QVERIFY(server.waitFor([&] { return server.registrationKeepAlives() >= 4; }, 3000));
    QVERIFY(recorder.failures.isEmpty()); // Longer than the ordinary network deadline.
    QVERIFY(server.registrationOpen());
    session.submitCaptcha(challengeId + 1, QStringLiteral("stale-token"));
    QTest::qWait(50);
    QVERIFY(server.registrationRetries().isEmpty());
    session.submitCaptcha(challengeId, QStringLiteral("private-token+"));
    QVERIFY(server.waitFor([&] { return !recorder.failures.isEmpty(); }));
    QCOMPARE(cleared.size(), 1);
    QCOMPARE(server.registrationRetries().size(), 1);
    const QList<QByteArray> fields = server.registrationRetries().first().split(':');
    QCOMPARE(fields.size(), 28);
    QCOMPARE(fields.last(), QByteArrayLiteral("private-token+"));
    QList<QByteArray> originalFields = server.registrationCommand().split(':');
    originalFields.last() = QByteArrayLiteral("private-token+");
    QCOMPARE(fields, originalFields);
    QVERIFY(server.registrationHandshakeDone());
    QCOMPARE(recorder.failures.first().code, ErrorCode::Authentication); // status 11, after command 8
    QVERIFY(!recorder.failures.first().summary().contains(QStringLiteral("private-")));
    for (const auto &entry : std::as_const(recorder.diagnostics))
        QVERIFY(!entry.contains(QStringLiteral("private-")));
    QCOMPARE(session.state(), SessionState::Failed);
    QCOMPARE(server.relayConnectionCount(), 0);
}

void TestSession::registrationCaptchaTimeoutAndCancel()
{
    for (const bool cancel : {false, true}) {
        uvtest::MockUltraviewServer server;
        auto options = mockOptions();
        options.captchaChallenges = {QByteArrayLiteral("60:private-challenge:")};
        server.setOptions(options);
        QVERIFY(server.listen());
        SessionManager session;
        SessionRecorder recorder;
        recorder.attach(&session);
        QSignalSpy captcha(&session, &SessionManager::captchaRequired);
        QSignalSpy cleared(&session, &SessionManager::captchaCleared);
        SessionRequest request = makeRequest(server);
        request.captchaTimeoutMs = 250;
        request.captchaKeepAliveMs = 50;
        session.start(request);
        QVERIFY(server.waitFor([&] { return captcha.size() == 1; }));
        if (cancel) {
            session.stop();
            QVERIFY(server.waitFor([&] { return recorder.stopped == 1; }));
            QVERIFY(recorder.failures.isEmpty());
            QCOMPARE(session.state(), SessionState::Disconnected);
        } else {
            QVERIFY(server.waitFor([&] { return !recorder.failures.isEmpty(); }));
            QCOMPARE(recorder.failures.first().code, ErrorCode::Timeout);
            QVERIFY(!recorder.failures.first().summary().contains(QStringLiteral("private")));
        }
        QCOMPARE(cleared.size(), 1);
        QVERIFY(server.waitFor([&] { return !server.registrationOpen(); }));
        QVERIFY(server.registrationRetries().isEmpty());
    }
}

void TestSession::registrationCaptchaInvalidAndRetryLimit()
{
    for (const bool invalid : {true, false}) {
        uvtest::MockUltraviewServer server;
        auto options = mockOptions();
        options.captchaChallenges = {"60:a:", "60:b:", "60:c:", "60:d:"};
        server.setOptions(options);
        QVERIFY(server.listen());
        SessionManager session;
        SessionRecorder recorder;
        recorder.attach(&session);
        QSignalSpy captcha(&session, &SessionManager::captchaRequired);
        session.start(makeRequest(server));
        const int count = invalid ? 1 : 3;
        for (int i = 0; i < count; ++i) {
            QVERIFY(server.waitFor([&] { return captcha.size() == i + 1; }));
            session.submitCaptcha(captcha.at(i).at(0).toULongLong(),
                                  invalid ? QStringLiteral("bad:token") : QStringLiteral("token"));
        }
        QVERIFY(server.waitFor([&] { return !recorder.failures.isEmpty(); }));
        QCOMPARE(recorder.failures.first().code, ErrorCode::Protocol);
        QCOMPARE(server.registrationRetries().size(), invalid ? 0 : 3);
    }
}

void TestSession::unrelatedRegistrationPayloadIsIgnored()
{
    uvtest::MockUltraviewServer server;
    auto options = mockOptions();
    options.preIdentityPayloads = {QByteArrayLiteral("99:synthetic-notice")};
    options.routeStatus = QByteArrayLiteral("11");
    server.setOptions(options);
    QVERIFY(server.listen());

    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    session.start(makeRequest(server));
    QVERIFY(server.waitFor([&] { return !recorder.failures.isEmpty(); }));
    QVERIFY(server.registrationHandshakeDone());
    QCOMPARE(recorder.failures.first().code, ErrorCode::Authentication);
}

void TestSession::selectedRegistrationProfileIsUsed()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    const QString path = directory.filePath(QStringLiteral("registration.json"));
    QStringList profileTerms = uvtest::testRegistrationTerms();
    profileTerms[4] = QString::fromUtf8("máy-thử");
    QJsonArray terms;
    for (const QString &term : profileTerms)
        terms.append(term);
    const QJsonObject object{{QStringLiteral("terms"), terms},
                             {QStringLiteral("ansi_codec"), QStringLiteral("utf-8")}};
    QFile profile(path);
    QVERIFY(profile.open(QIODevice::WriteOnly));
    QVERIFY(profile.write(QJsonDocument(object).toJson()) > 0);
    profile.close();

    // A previously stored path alone must not reactivate a potentially empty
    // scaffold after an upgrade; the user must opt in.
    {
        QSettings oldSettings(QSettings::IniFormat, QSettings::UserScope,
                              QStringLiteral("ultraview"), QStringLiteral("client"));
        oldSettings.setValue(QStringLiteral("profiles/registration"), path);
        oldSettings.sync();
    }

    uvtest::MockUltraviewServer server;
    auto options = mockOptions();
    options.routeStatus = QByteArrayLiteral("11");
    server.setOptions(options);
    QVERIFY(server.listen());

    models::ConnectionViewModel controller;
    models::SettingsModel *settings = controller.settingsObject();
    QVERIFY(!settings->registrationProfileEnabled());
    QCOMPARE(settings->registrationProfilePath(), path);
    settings->setRegistrationProfileEnabled(true);
    settings->setRegistrationHostOverride(QStringLiteral("127.0.0.1"));
    settings->setRegistrationPortOverride(server.port());
    settings->setRegistrationHttpHostOverride(QStringLiteral("functions3.ultraviewer.test"));
    settings->setTlsMode(int(security::TlsPolicy::Mode::AcceptAnyCertificate)); // loopback test only
    settings->setRememberPassword(false);
    controller.setPartnerId(QStringLiteral("123456789"));
    controller.setPassword(QStringLiteral("test-password"));
    controller.connectNow();

    QVERIFY(server.waitFor([&] { return controller.stateCode() == int(SessionState::Failed); }));
    QVERIFY(server.registrationHandshakeDone());
    QCOMPARE(server.registrationCommand(), RegistrationCodec::buildRegistrationCommand(
                                             profileTerms, AnsiCodec::utf8()));
    models::SettingsModel reloaded;
    QVERIFY(reloaded.registrationProfileEnabled());
    QCOMPARE(reloaded.registrationProfilePath(), path);

    // Selecting a missing file must fail locally, not silently use another identity.
    settings->setRegistrationProfilePath(directory.filePath(QStringLiteral("missing.json")));
    QSignalSpy errorSpy(&controller, &models::ConnectionViewModel::errorRaised);
    controller.connectNow();
    QCOMPARE(errorSpy.size(), 1);
    QVERIFY(!controller.errorText().isEmpty());
}

void TestSession::relayLoginTimeout()
{
    uvtest::MockUltraviewServer server;
    auto options = mockOptions();
    options.muteRelay = true;
    server.setOptions(options);
    QVERIFY(server.listen());

    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    session.start(makeRequest(server));

    QVERIFY(server.waitFor([&] { return server.relayLoginDone(); }));
    QVERIFY(server.waitFor([&] { return !recorder.failures.isEmpty(); }, 5000));
    QCOMPARE(recorder.failures.first().code, ErrorCode::Timeout);
    QVERIFY(!session.isConnected());
    QCOMPARE(session.state(), SessionState::Failed);
}

void TestSession::relayClosedByPartner()
{
    uvtest::MockUltraviewServer server;
    auto options = mockOptions();
    options.closeRelayAfterLogin = true;
    server.setOptions(options);
    QVERIFY(server.listen());

    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    session.start(makeRequest(server));

    QVERIFY(server.waitFor([&] { return !recorder.failures.isEmpty(); }, 5000));
    // The partner closed the channel: a network-level end of session, not a crash.
    QCOMPARE(int(recorder.failures.first().code), int(ErrorCode::Network));
    QCOMPARE(session.state(), SessionState::Failed);
}

void TestSession::audioAndUnknownKindsAreRefusedButKeepTheSession()
{
    // Commands 42 (silence) and 43 (G.722 audio) are decoded and consumed; the
    // microphone kinds 50/51 are still unsupported and an unknown kind is still
    // refused explicitly, and none of them ends the session.
    const auto packet = [](quint8 kind, const QByteArray &field = QByteArray()) {
        return encodeByteArrayFields({QByteArray(1, static_cast<char>(kind)), field});
    };
    QByteArray silenceCount(4, '\0');
    silenceCount[0] = static_cast<char>(0x40);
    silenceCount[1] = static_cast<char>(0x01);

    uvtest::MockUltraviewServer server;
    auto options = mockOptions();
    options.relayPackets = {packet(42, silenceCount), packet(43, QByteArrayLiteral("\x01\x02\x03\x04")),
                            packet(50), packet(51), packet(77), packet(3)};
    server.setOptions(options);
    QVERIFY(server.listen());

    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    session.start(makeRequest(server));

    QVERIFY(server.waitFor([&] { return recorder.notices.size() >= 3; }, 5000));
    QCOMPARE(server.relayConnectionCount(), 1);
    QVERIFY(session.isConnected());
    QVERIFY(recorder.failures.isEmpty());
    QVERIFY(recorder.hasNotice(ErrorCode::UnsupportedOperation));
    for (const ErrorInfo &note : recorder.notices) {
        QVERIFY(!note.message.isEmpty());
        // The UI message names the operation, not a raw packet.
        QVERIFY(!note.message.contains(QStringLiteral("PyObject")));
    }
    session.stop();
    QVERIFY(server.waitFor([&] { return recorder.stopped == 1; }));
}

void TestSession::bitBltUpdateIsRefusedFatally()
{
    // Field 5 of a kind-1 update is a BitBlt record.  The reference silently
    // ignores it; this client refuses it explicitly and ends the session instead of
    // showing a screen that is wrong in ways the user cannot see.
    const QJsonObject sequence = uvtest::Fixtures::instance()
                                    .array(QStringLiteral("screen_sequences"))
                                    .at(5) // bitblt_unsupported
                                    .toObject();
    QCOMPARE(sequence.value(QStringLiteral("name")).toString(), QStringLiteral("bitblt_unsupported"));
    const QByteArray packet = uvtest::Fixtures::entryBytes(
        sequence.value(QStringLiteral("updates")).toArray().at(0).toObject(), QStringLiteral("packet_hex"));

    uvtest::MockUltraviewServer server;
    auto options = mockOptions();
    options.relayPackets = {packet};
    server.setOptions(options);
    QVERIFY(server.listen());

    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    session.start(makeRequest(server));

    QVERIFY(server.waitFor([&] { return !recorder.failures.isEmpty(); }, 5000));
    QCOMPARE(recorder.failures.first().code, ErrorCode::UnsupportedOperation);
    QVERIFY(recorder.failures.first().summary().contains(QStringLiteral("BitBlt")));
    QCOMPARE(session.state(), SessionState::Failed);
}

void TestSession::kind3SizeFollowUp()
{
    // A kind-3 packet with a 4-byte field must be answered with b"|||4<Int32LE>".
    QByteArray field(4, '\0');
    field[0] = static_cast<char>(0x78);
    field[1] = static_cast<char>(0x56);
    field[2] = static_cast<char>(0x34);
    field[3] = static_cast<char>(0x12);
    const QByteArray packet = encodeByteArrayFields({QByteArray(1, '\x03'), field});

    uvtest::MockUltraviewServer server;
    auto options = mockOptions();
    options.relayPackets = {packet};
    server.setOptions(options);
    QVERIFY(server.listen());

    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    session.start(makeRequest(server));

    QVERIFY(server.waitForRelayPayload(QByteArrayLiteral("|||4") + QByteArrayLiteral("305419896")));
    QVERIFY(session.isConnected());
    // Statistics are published at most every 400 ms so the UI signal rate stays
    // bounded, so the counter may still be throttled at this point.
    QTRY_COMPARE_WITH_TIMEOUT(session.statistics().sizeFollowUps, 1u, 3000);

    QVERIFY(!recorder.hasNotice(ErrorCode::UnsupportedOperation));
    session.stop();
    QVERIFY(server.waitFor([&] { return recorder.stopped == 1; }));
}

void TestSession::coalescedFramesFlushWhenRendererCatchesUp()
{
    uvtest::MockUltraviewServer server;
    auto options = mockOptions();
    const auto updates = uvtest::Fixtures::instance().array(QStringLiteral("screen_sequences"))
                             .at(1).toObject().value(QStringLiteral("updates")).toArray();
    for (const auto &update : updates)
        options.relayPackets.append(uvtest::Fixtures::entryBytes(update.toObject(), QStringLiteral("packet_hex")));
    server.setOptions(options);
    QVERIFY(server.listen());
    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session); // deliberately do not consume frames yet
    session.start(makeRequest(server));
    QVERIFY(server.waitFor([&] { return session.statistics().updatesDecoded == 2; }, 3000));
    QCOMPARE(recorder.frames.size(), 2); // initial black + first update, window full
    session.releaseFrames(1);
    QVERIFY(server.waitFor([&] { return recorder.frames.size() == 3; }, 3000));
    // No new network packet triggered the last patch: releasing capacity did.
    QCOMPARE(QCryptographicHash::hash(recorder.mirrorRgb888(), QCryptographicHash::Sha256).toHex(),
             updates.last().toObject().value(QStringLiteral("canvas_sha256")).toString().toLatin1());
    session.stop();
    QVERIFY(server.waitFor([&] { return recorder.stopped == 1; }));
}

void TestSession::reconnectStartsWithFreshState()
{
    uvtest::MockUltraviewServer first;
    first.setOptions(mockOptions());
    QVERIFY(first.listen());
    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    session.start(makeRequest(first));
    QVERIFY(first.waitFor([&] { return session.isConnected() && recorder.frames.size() == 1; }));
    QVERIFY(session.statistics().dictionaryInstalled);
    session.stop();
    QVERIFY(first.waitFor([&] { return recorder.stopped == 1; }));
    QVERIFY(session.desktopSize().isEmpty());
    QVERIFY(session.route().tcpServerIp.isEmpty());

    uvtest::MockUltraviewServer second;
    second.setOptions(mockOptions());
    QVERIFY(second.listen());
    auto request = makeRequest(second);
    request.screenDictionary.clear(); // no dictionary must not reuse the old one
    session.start(request);
    QVERIFY(second.waitFor([&] { return session.isConnected() && recorder.frames.size() == 2; }));
    QTRY_VERIFY_WITH_TIMEOUT(!session.statistics().dictionaryInstalled, 3000);
    QVERIFY(recorder.frames.last()->fullFrame);
    QVERIFY(recorder.failures.isEmpty());
    session.stop();
    QVERIFY(second.waitFor([&] { return recorder.stopped == 2; }));
}

void TestSession::disconnectNotifiesPartner()
{
    uvtest::MockUltraviewServer server;
    server.setOptions(mockOptions());
    QVERIFY(server.listen());
    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    session.start(makeRequest(server));
    QVERIFY(server.waitFor([&] { return session.isConnected(); }));
    session.stop();
    QVERIFY(server.waitForRelayPayload(QByteArrayLiteral("19")));
    QVERIFY(server.waitFor([&] { return recorder.stopped == 1; }));
    QVERIFY(recorder.failures.isEmpty());
}

void TestSession::reconnectReceivesScreenUpdates()
{
    const QByteArray packet = uvtest::Fixtures::entryBytes(
        rgb555Sequence().value(QStringLiteral("updates")).toArray().at(0).toObject(),
        QStringLiteral("packet_hex"));
    auto options = mockOptions();
    options.relayPackets = {packet};
    uvtest::MockUltraviewServer first;
    first.setOptions(options);
    QVERIFY(first.listen());
    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    connect(&session, &SessionManager::frameReady, &session,
            [&session] { session.releaseFrames(1); });
    session.start(makeRequest(first));
    QVERIFY(first.waitFor([&] { return recorder.frames.size() >= 2; }));
    session.stop();
    QVERIFY(first.waitFor([&] { return recorder.stopped == 1; }));

    uvtest::MockUltraviewServer second;
    second.setOptions(options);
    QVERIFY(second.listen());
    const int before = recorder.frames.size();
    session.start(makeRequest(second));
    QVERIFY(second.waitFor([&] { return recorder.frames.size() >= before + 2; }));
    QVERIFY(session.isConnected());
    QVERIFY(recorder.failures.isEmpty());
    session.stop();
    QVERIFY(second.waitFor([&] { return recorder.stopped == 2; }));
}

void TestSession::registrationChannelLossDoesNotBreakTheSession()
{
    uvtest::MockUltraviewServer server;
    server.setOptions(mockOptions());
    QVERIFY(server.listen());

    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    session.start(makeRequest(server));

    QVERIFY(server.waitFor([&] { return server.relayLoginDone(); }));
    // The mock records the login when it receives the frame; the session becomes
    // Connected only after it has processed the kind-0 acknowledgement.
    QVERIFY(server.waitFor([&] { return session.state() == SessionState::Connected; }, 3000));
    QVERIFY(session.isConnected());
    QVERIFY(server.registrationOpen());

    // The reference stops using the registration channel once the route is known;
    // losing it must not tear a running session down and must not raise a visible
    // warning: it belongs in the log as a diagnostic only.
    server.dropRegistration();
    QVERIFY(server.waitFor([&] { return !server.registrationOpen(); }));
    QVERIFY(server.waitFor([&] { return !recorder.diagnostics.isEmpty(); }, 3000));
    QVERIFY(recorder.failures.isEmpty());
    QVERIFY(recorder.notices.isEmpty());
    QVERIFY(session.isConnected());
    QVERIFY(recorder.diagnostics.first().contains(QStringLiteral("registration channel")));

    session.stop();
    QVERIFY(server.waitFor([&] { return recorder.stopped == 1; }));
}

void TestSession::inputCommandsReachThePartner()
{
    uvtest::MockUltraviewServer server;
    server.setOptions(mockOptions());
    QVERIFY(server.listen());

    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    session.start(makeRequest(server));
    QVERIFY(server.waitFor([&] { return session.state() == SessionState::Connected; }, 3000));

    const QByteArray move = InputCodec::mouseMove(100, 200);
    const QByteArray click = InputCodec::mouseButton(MouseButton::Left, true);
    const QByteArray key = InputCodec::keyboard(0x41, true);
    session.sendInput(move);
    session.sendInput(click);
    session.sendInput(key);
    QVERIFY(server.waitForRelayPayload(move));
    QVERIFY(server.waitForRelayPayload(click));
    QVERIFY(server.waitForRelayPayload(key));
    for (const QByteArray &payload : {move, click, key})
        QVERIFY(server.receivedRelayPayload(payload));

    session.stop();
    QVERIFY(server.waitFor([&] { return recorder.stopped == 1; }));
}

void TestSession::invalidRequestIsRejectedLocally()
{
    uvtest::MockUltraviewServer server;
    server.setOptions(mockOptions());
    QVERIFY(server.listen());

    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    SessionRequest request = makeRequest(server);
    request.registrationTerms.removeLast(); // 25 terms
    session.start(request);

    QVERIFY(server.waitFor([&] { return !recorder.failures.isEmpty(); }));
    QCOMPARE(recorder.failures.first().code, ErrorCode::Configuration);
    // No connection was attempted at all.
    QCOMPARE(server.prefaces().size(), 0);
    QCOMPARE(server.relayConnectionCount(), 0);
    QCOMPARE(session.state(), SessionState::Failed);
}

void TestSession::certificateNameMismatchIsRefused()
{
    uvtest::MockUltraviewServer server;
    server.setOptions(mockOptions());
    QVERIFY(server.listen());

    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    SessionRequest request = makeRequest(server);
    // The test certificate is valid for "ultraviewer"/"localhost"; a different name
    // must be refused, which proves verification is really active.
    request.registrationPeerName = QStringLiteral("wrong-name.invalid");
    request.timeoutMs = 2000;
    session.start(request);

    QVERIFY(server.waitFor([&] { return !recorder.failures.isEmpty(); }, 5000));
    QVERIFY(recorder.hasFailure(ErrorCode::Tls) || recorder.hasFailure(ErrorCode::Network));
    QCOMPARE(server.relayConnectionCount(), 0);
    QCOMPARE(session.state(), SessionState::Failed);
}

void TestSession::registrationTlsAddressFallback()
{
    uvtest::MockUltraviewServer bad;
    auto badOptions = mockOptions();
    const QString dir = uvtest::Fixtures::instance().dataDirectory();
    badOptions.certificateFile = dir + QStringLiteral("/tls/endpoint.crt");
    badOptions.privateKeyFile = dir + QStringLiteral("/tls/endpoint.key");
    bad.setOptions(badOptions);
    QVERIFY(bad.listen());
    uvtest::MockUltraviewServer good;
    QVERIFY(good.listen(QHostAddress(QStringLiteral("127.0.0.2")), bad.port()));

    RegistrationTransport transport;
    RegistrationTransport::Options options;
    options.host = QStringLiteral("127.0.0.1");
    options.port = bad.port();
    options.httpHostHeader = QStringLiteral("registration.test");
    options.peerNameOverride = QStringLiteral("ultraviewer");
    options.tlsPolicy = testPolicy();
    options.timeoutMs = 2000;
    options.fallbackAddressesForTesting = {QHostAddress(QStringLiteral("127.0.0.1")),
                                           QHostAddress(QStringLiteral("127.0.0.2")),
                                           QHostAddress(QStringLiteral("127.0.0.2"))};
    transport.setOptions(options);
    QSignalSpy ready(&transport, &RegistrationTransport::ready);
    QSignalSpy failed(&transport, &RegistrationTransport::failed);
    QSignalSpy retry(&transport, &RegistrationTransport::retryingVerifiedAddress);
    transport.start();
    QVERIFY(good.waitFor([&] { return !ready.isEmpty() || !failed.isEmpty(); }));
    QCOMPARE(ready.size(), 1);
    QCOMPARE(failed.size(), 0);
    QCOMPARE(retry.size(), 1); // failed and duplicate addresses were excluded
    QVERIFY(transport.peerDescription().startsWith(QStringLiteral("127.0.0.2:")));
    QVERIFY(!bad.registrationOpen()); // no command or HTTP preface sent to the rejected certificate
    QVERIFY(good.registrationOpen());
    transport.stop();
}

void TestSession::registrationTlsAddressFallbackFailsClosed()
{
    uvtest::MockUltraviewServer first;
    QVERIFY(first.listen());
    uvtest::MockUltraviewServer second;
    QVERIFY(second.listen(QHostAddress(QStringLiteral("127.0.0.2")), first.port()));

    RegistrationTransport transport;
    RegistrationTransport::Options options;
    options.host = QStringLiteral("127.0.0.1");
    options.port = first.port();
    options.peerNameOverride = QStringLiteral("wrong-name.invalid");
    options.tlsPolicy = testPolicy(); // trusted CA, but neither certificate matches this name
    options.timeoutMs = 2000;
    options.fallbackAddressesForTesting = {QHostAddress(QStringLiteral("127.0.0.2"))};
    transport.setOptions(options);
    QSignalSpy ready(&transport, &RegistrationTransport::ready);
    QSignalSpy failed(&transport, &RegistrationTransport::failed);
    QSignalSpy retry(&transport, &RegistrationTransport::retryingVerifiedAddress);
    transport.start();
    QVERIFY(second.waitFor([&] { return !failed.isEmpty(); }));
    QCOMPARE(retry.size(), 1);
    QCOMPARE(ready.size(), 0);
    QCOMPARE(failed.size(), 1);
    const auto error = qvariant_cast<ErrorInfo>(failed.first().first());
    QCOMPARE(error.code, ErrorCode::Tls);
    QVERIFY(error.summary().contains(QStringLiteral("wrong-name.invalid")));
    QVERIFY(!first.registrationOpen());
    QVERIFY(!second.registrationOpen());
}

void TestSession::registrationTlsAddressFallbackCanBeCancelled()
{
    uvtest::MockUltraviewServer first;
    QVERIFY(first.listen());
    uvtest::MockUltraviewServer second;
    QVERIFY(second.listen(QHostAddress(QStringLiteral("127.0.0.2")), first.port()));
    RegistrationTransport transport;
    RegistrationTransport::Options options;
    options.host = QStringLiteral("127.0.0.1");
    options.port = first.port();
    options.peerNameOverride = QStringLiteral("wrong-name.invalid");
    options.tlsPolicy = testPolicy();
    options.fallbackAddressesForTesting = {QHostAddress(QStringLiteral("127.0.0.2"))};
    transport.setOptions(options);
    QSignalSpy retry(&transport, &RegistrationTransport::retryingVerifiedAddress);
    QSignalSpy ready(&transport, &RegistrationTransport::ready);
    QSignalSpy failed(&transport, &RegistrationTransport::failed);
    QObject::connect(&transport, &RegistrationTransport::retryingVerifiedAddress,
                     &transport, &RegistrationTransport::stop);
    transport.start();
    QVERIFY(first.waitFor([&] { return !retry.isEmpty(); }));
    QTest::qWait(100);
    QVERIFY(ready.isEmpty());
    QVERIFY(failed.isEmpty());
    QVERIFY(!second.registrationOpen());
}

void TestSession::optionalLiveTlsFallback()
{
    // Opt-in manual probe only: CTest never sets these variables or uses the
    // network. No registration command or partner credential is sent.
    const QString bad = qEnvironmentVariable("ULTRAVIEW_TEST_TLS_BAD_IP");
    const QString good = qEnvironmentVariable("ULTRAVIEW_TEST_TLS_GOOD_IP");
    const QString host = qEnvironmentVariable("ULTRAVIEW_TEST_TLS_HOST");
    if (bad.isEmpty() || good.isEmpty() || host.isEmpty())
        QSKIP("Live TLS fallback probe is opt-in");
    RegistrationTransport transport;
    RegistrationTransport::Options options;
    options.host = bad;
    options.port = 443;
    options.httpHostHeader = host;
    options.peerNameOverride = host;
    options.timeoutMs = 7000;
    options.fallbackAddressesForTesting = {QHostAddress(good)};
    transport.setOptions(options);
    QSignalSpy ready(&transport, &RegistrationTransport::ready);
    QSignalSpy failed(&transport, &RegistrationTransport::failed);
    QSignalSpy retry(&transport, &RegistrationTransport::retryingVerifiedAddress);
    transport.start();
    QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !failed.isEmpty(), 20000);
    if (!failed.isEmpty())
        qWarning() << qvariant_cast<ErrorInfo>(failed.first().first()).summary();
    QCOMPARE(retry.size(), 1);
    QCOMPARE(failed.size(), 0);
    QCOMPARE(ready.size(), 1);
    QVERIFY(transport.peerDescription().startsWith(good + QLatin1Char(':')));
    transport.stop();
}

void TestSession::registrationDnsAddressFallback()
{
    // Exercise the real async DNS path when localhost has two usable families.
    const auto resolved = QHostInfo::fromName(QStringLiteral("localhost")).addresses();
    QList<QHostAddress> loopbacks;
    for (const auto &address : resolved) {
        if (address.isLoopback() && !loopbacks.contains(address))
            loopbacks.append(address);
    }
    if (loopbacks.size() < 2)
        QSKIP("localhost does not resolve to two loopback addresses");

    uvtest::MockUltraviewServer bad;
    if (!bad.listen(loopbacks.first()))
        QSKIP("first loopback family is unavailable");
    uvtest::MockUltraviewServer good;
    auto goodOptions = mockOptions();
    const QString dir = uvtest::Fixtures::instance().dataDirectory();
    goodOptions.certificateFile = dir + QStringLiteral("/tls/endpoint.crt");
    goodOptions.privateKeyFile = dir + QStringLiteral("/tls/endpoint.key");
    good.setOptions(goodOptions);
    if (!good.listen(loopbacks.at(1), bad.port()))
        QSKIP("second loopback family is unavailable");

    RegistrationTransport transport;
    RegistrationTransport::Options options;
    options.host = QStringLiteral("localhost");
    options.port = bad.port();
    options.timeoutMs = 2000;
    options.tlsPolicy.setAdditionalCaCertificateFile(good.certificatePath());
    transport.setOptions(options);
    QSignalSpy retry(&transport, &RegistrationTransport::retryingVerifiedAddress);
    QSignalSpy ready(&transport, &RegistrationTransport::ready);
    QSignalSpy failed(&transport, &RegistrationTransport::failed);
    transport.start();
    QVERIFY(good.waitFor([&] { return !ready.isEmpty() || !failed.isEmpty(); }));
    if (retry.isEmpty() && !ready.isEmpty())
        QSKIP("socket selected the good loopback address first");
    QCOMPARE(retry.size(), 1);
    QCOMPARE(failed.size(), 0);
    QCOMPARE(ready.size(), 1);
    QVERIFY(good.registrationOpen());
    QVERIFY(!bad.registrationOpen());
    transport.stop();
}

void TestSession::defaultTlsNamesUseTheEndpoint()
{
    uvtest::MockUltraviewServer server;
    auto options = mockOptions();
    const auto directory = uvtest::Fixtures::instance().dataDirectory();
    options.certificateFile = directory + QStringLiteral("/tls/endpoint.crt");
    options.privateKeyFile = directory + QStringLiteral("/tls/endpoint.key");
    server.setOptions(options);
    QVERIFY(server.listen());
    auto request = makeRequest(server);
    request.registrationPeerName.clear();
    security::TlsPolicy policy;
    policy.setAdditionalCaCertificateFile(server.certificatePath());
    request.tlsPolicy = policy;

    // This certificate has IP 127.0.0.1 / DNS localhost, NOT "ultraviewer".
    // Hard-coding that reference literal fails even with a trusted certificate.
    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    session.start(request);
    QVERIFY(server.waitFor([&] { return session.isConnected() || !recorder.failures.isEmpty(); }));
    QVERIFY2(session.isConnected(), "default TLS verification must use each connect endpoint");
    QVERIFY(recorder.failures.isEmpty());
    QVERIFY(session.statistics().tlsWarning.isEmpty());
    session.stop();
    QVERIFY(server.waitFor([&] { return recorder.stopped == 1; }));
}

void TestSession::tlsPolicyAddsServiceChainCertificates()
{
    // The production relay sends only its leaf while the registration endpoint
    // also sends the intermediate.  The verified registration chain is reused as
    // an additional trust anchor; the system store stays in place and duplicates
    // do not accumulate.
    security::TlsPolicy policy;
    const QList<QSslCertificate> systemStore = policy.configurationFor().caCertificates();
    const QList<QSslCertificate> serviceChain =
        QSslCertificate::fromPath(uvtest::Fixtures::instance().tlsCertificatePath());
    QVERIFY(!serviceChain.isEmpty());

    policy.addCaCertificates(serviceChain);
    const QList<QSslCertificate> extendedStore = policy.configurationFor().caCertificates();
    QCOMPARE(extendedStore.size(), systemStore.size() + serviceChain.size());
    for (const QSslCertificate &certificate : serviceChain)
        QVERIFY(extendedStore.contains(certificate));

    policy.addCaCertificates(serviceChain);
    QCOMPARE(policy.configurationFor().caCertificates().size(), extendedStore.size());
}

void TestSession::relayAuthenticatesTheRegistrationCertificateName()
{
    uvtest::MockUltraviewServer server;
    auto options = mockOptions();
    // The route field becomes the HTTP `Host:` header of the relay preface in the
    // reference; it is not a certificate name.  An unrelated value must neither
    // be verified against the relay certificate nor break the session.
    options.routeHostname = QStringLiteral("wrong-relay.invalid");
    server.setOptions(options);
    QVERIFY(server.listen());
    auto request = makeRequest(server);
    // Registration's explicit "ultraviewer" override matches the test certificate;
    // the relay authenticates that same service name, like the reference does.
    request.relayHostOverride.clear();
    request.relayHttpHostOverride.clear(); // the route value becomes the Host header

    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    session.start(request);
    QVERIFY(server.waitFor([&] { return server.relayLoginDone(); }, 5000));
    QVERIFY(server.waitFor([&] { return session.state() == SessionState::Connected; }, 3000));
    QVERIFY(recorder.failures.isEmpty());
    QCOMPARE(server.prefaces().size(), 2);
    QVERIFY(server.prefaces().at(1).contains("Host: wrong-relay.invalid\r\n"));
    session.stop();
    QVERIFY(server.waitFor([&] { return recorder.stopped == 1; }));
}

void TestSession::relayCertificateNameOverrideIsRefusedWhenItDoesNotMatch()
{
    uvtest::MockUltraviewServer server;
    server.setOptions(mockOptions());
    QVERIFY(server.listen());
    auto request = makeRequest(server);
    // An explicit relay certificate name is really verified: a name the test
    // certificate does not carry must fail before the relay login is sent.
    request.relayPeerName = QStringLiteral("wrong-relay.invalid");

    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    session.start(request);
    QVERIFY(server.waitFor([&] { return !recorder.failures.isEmpty(); }));
    QVERIFY(server.routeSent()); // registration TLS + command 3/8 succeeded
    QVERIFY(recorder.hasFailure(ErrorCode::Tls));
    QVERIFY(recorder.failures.first().detail.contains(QStringLiteral("wrong-relay.invalid")));
    QVERIFY(!server.relayLoginDone()); // no login sent to a name-mismatched relay
    QVERIFY(!session.isConnected());
}

void TestSession::acceptAnyCertificateIsExplicit()
{
    uvtest::MockUltraviewServer server;
    server.setOptions(mockOptions());
    QVERIFY(server.listen());

    SessionManager session;
    SessionRecorder recorder;
    recorder.attach(&session);
    SessionRequest request = makeRequest(server);
    // The original program accepts every certificate.  That behaviour is only
    // reachable by choosing it explicitly, and the session reports the warning.
    security::TlsPolicy insecure(security::TlsPolicy::Mode::AcceptAnyCertificate);
    request.tlsPolicy = insecure;
    request.registrationPeerName = QStringLiteral("wrong-name.invalid");
    session.start(request);

    QVERIFY(server.waitFor([&] { return server.relayLoginDone(); }, 5000));
    QVERIFY(server.waitFor([&] { return session.state() == SessionState::Connected; }, 3000));
    QVERIFY(session.isConnected());
    QVERIFY(recorder.failures.isEmpty());
    const SessionStatistics statistics = session.statistics();
    QVERIFY(!statistics.tlsWarning.isEmpty());
    QVERIFY(statistics.tlsWarning.contains(QStringLiteral("DISABLED")));
    session.stop();
    QVERIFY(server.waitFor([&] { return recorder.stopped == 1; }));
}

QTEST_MAIN(TestSession)
#include "tst_session.moc"
