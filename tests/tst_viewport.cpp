//
// Tests for render/ViewportMap (item <-> remote desktop coordinates) and
// security/TlsPolicy (verification modes).
//
// The viewport mapping is client-side glue but a one-pixel error is a visible
// click offset on the partner side, so every mode is covered here.  The TLS policy
// tests pin the *only* place where certificate verification may be relaxed.
//
#include "TestSupport.h"

#include "core/ErrorTypes.h"
#include "core/SessionState.h"
#include "render/ViewportMap.h"
#include "security/TlsPolicy.h"

#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <QtTest>

using namespace ultraview;
using namespace ultraview::render;
using namespace ultraview::security;

class TestViewport : public QObject {
    Q_OBJECT

private slots:
    void preserveAspectFit();
    void preserveAspectCrop();
    void stretch();
    void mapRoundTrip();
    void rejectsOutsidePoints();
    void degenerateGeometry();
    void tlsPolicyDefaults();
    void tlsPolicyIgnoresOnlyHostNameMismatch();
    void tlsPolicyAcceptAny();
    void tlsPolicySettingsRoundTrip();
    void tlsPolicyAdditionalCa();
    void sessionStateNames();
};

void TestViewport::preserveAspectFit()
{
    // A 16:9 desktop inside a 4:3 item is letter-boxed: the content keeps the
    // desktop aspect ratio, is centred and leaves black bars top and bottom.
    ViewportMap map(QSizeF(800, 600), QSize(1920, 1080));
    const QRectF content = map.contentRect();
    QCOMPARE(content.width(), 800.0);
    QCOMPARE(content.height(), 800.0 * (1080.0 / 1920.0));
    QCOMPARE(content.x(), 0.0);
    QCOMPARE(content.y(), (600.0 - content.height()) / 2.0);
    QCOMPARE(map.scaleFactors().width(), map.scaleFactors().height());
    QVERIFY(map.isValid());

    // 1:1 mapping when the item has exactly the desktop size.
    ViewportMap exact(QSizeF(1920, 1080), QSize(1920, 1080));
    QCOMPARE(exact.contentRect(), QRectF(0, 0, 1920, 1080));
    QCOMPARE(exact.scaleFactors(), QSizeF(1.0, 1.0));
    QPointF source;
    QVERIFY(exact.mapItemToSource(QPointF(10, 20), &source));
    QCOMPARE(source, QPointF(10, 20));
    QCOMPARE(exact.mapSourceToItem(QPointF(10, 20)), QPointF(10, 20));
}

void TestViewport::preserveAspectCrop()
{
    ViewportMap map(QSizeF(800, 600), QSize(1920, 1080), ViewportMap::Mode::PreserveAspectCrop);
    const QRectF content = map.contentRect();
    // Crop fills the item and cuts the overflowing axis (horizontal here).
    QCOMPARE(content.height(), 600.0);
    QCOMPARE(content.width(), 600.0 * (1920.0 / 1080.0));
    QCOMPARE(content.y(), 0.0);
    QCOMPARE(content.x(), (800.0 - content.width()) / 2.0);

    // The visible part maps into the middle of the desktop.
    QPointF source;
    QVERIFY(map.mapItemToSource(QPointF(400, 300), &source));
    QCOMPARE(source.x(), 960.0);
    QCOMPARE(source.y(), 540.0);
    QVERIFY(map.mapItemToSource(QPointF(0, 0), &source));
    QVERIFY(source.x() > 0.0);
    QVERIFY(source.x() < 960.0);
    QCOMPARE(source.y(), 0.0);
    QVERIFY(map.mapItemToSource(QPointF(799, 599), &source));
    QVERIFY(source.x() > 960.0);
    QVERIFY(source.x() <= 1919.0);
    // The item's bottom edge is exactly the content's bottom edge, so the last
    // item row stays inside the desktop without reaching its last row:
    // 599 * (1080 / 600) = 1078.2.  The mapping is continuous; the caller floors
    // the coordinate before it reaches the wire.
    QVERIFY(qAbs(source.y() - 1078.2) < 0.001);
    // Crop overflow is not an interactive part of the viewport.
    QVERIFY(!map.mapItemToSource(QPointF(-1, 300), &source));
    QVERIFY(!map.mapItemToSource(QPointF(801, 300), &source));
}

void TestViewport::stretch()
{
    ViewportMap map(QSizeF(400, 400), QSize(1920, 1080), ViewportMap::Mode::Stretch);
    QCOMPARE(map.contentRect(), QRectF(0, 0, 400, 400));
    QCOMPARE(map.scaleFactors().width(), 400.0 / 1920.0);
    QCOMPARE(map.scaleFactors().height(), 400.0 / 1080.0);
    QPointF source;
    QVERIFY(map.mapItemToSource(QPointF(200, 200), &source));
    QCOMPARE(source.x(), 960.0);
    QCOMPARE(source.y(), 540.0);
}

void TestViewport::mapRoundTrip()
{
    ViewportMap map(QSizeF(641, 377), QSize(1024, 768));
    // Points inside the letter-boxed content area (the bars are not clickable).
    const QRectF content = map.contentRect();
    for (const QPointF &itemPoint : {QPointF(content.left() + 1.0, 10.0), QPointF(300, 200),
                                     QPointF(content.right() - 1.0, 370.0)}) {
        QPointF source;
        QVERIFY(map.mapItemToSource(itemPoint, &source));
        const QPointF back = map.mapSourceToItem(source);
        QVERIFY(qAbs(back.x() - itemPoint.x()) < 0.001);
        QVERIFY(qAbs(back.y() - itemPoint.y()) < 0.001);
        // Every mapped point is a valid remote pixel.
        QVERIFY(qFloor(source.x()) >= 0);
        QVERIFY(qFloor(source.x()) < map.sourceSize().width());
        QVERIFY(qFloor(source.y()) >= 0);
        QVERIFY(qFloor(source.y()) < map.sourceSize().height());
    }
    // The bottom right content pixel maps inside the desktop, never past it.
    QPointF source;
    QVERIFY(map.mapItemToSource(QPointF(content.right(), content.bottom()), &source));
    QCOMPARE(qFloor(source.x()), 1023);
    QCOMPARE(qFloor(source.y()), 767);
}

void TestViewport::rejectsOutsidePoints()
{
    ViewportMap map(QSizeF(800, 600), QSize(1920, 1080)); // letter-boxed: bars top/bottom
    QPointF source;
    QVERIFY(!map.mapItemToSource(QPointF(-1, 10), &source));
    QVERIFY(!map.mapItemToSource(QPointF(801, 10), &source));
    QVERIFY(!map.mapItemToSource(QPointF(10, -1), &source));
    QVERIFY(!map.mapItemToSource(QPointF(10, 601), &source));
    // Inside the black bar there is no remote pixel.
    QVERIFY(!map.mapItemToSource(QPointF(400, 1), &source));
    QVERIFY(map.mapItemToSource(QPointF(400, 300), &source));
    // The edge of the content area is inclusive and maps to the first pixel.
    const QRectF content = map.contentRect();
    QVERIFY(map.mapItemToSource(QPointF(content.left(), content.top()), &source));
    QCOMPARE(source, QPointF(0, 0));
}

void TestViewport::degenerateGeometry()
{
    ViewportMap empty;
    QVERIFY(!empty.isValid());
    QPointF source;
    QVERIFY(!empty.mapItemToSource(QPointF(1, 1), &source));

    ViewportMap zeroItem(QSizeF(0, 0), QSize(1920, 1080));
    QVERIFY(!zeroItem.isValid());
    QVERIFY(!zeroItem.mapItemToSource(QPointF(1, 1), &source));

    ViewportMap zeroSource(QSizeF(100, 100), QSize(0, 0));
    QVERIFY(!zeroSource.isValid());
    QVERIFY(!zeroSource.mapItemToSource(QPointF(1, 1), &source));

    // The mapping is recomputed when the item is resized by the scene graph.
    ViewportMap resized(QSizeF(100, 100), QSize(200, 100));
    QCOMPARE(resized.contentRect(), QRectF(0, 25, 100, 50));
    resized.setItemSize(QSizeF(400, 400));
    QCOMPARE(resized.contentRect(), QRectF(0, 100, 400, 200));
    resized.setMode(ViewportMap::Mode::Stretch);
    QCOMPARE(resized.contentRect(), QRectF(0, 0, 400, 400));
    resized.setSourceSize(QSize(400, 400));
    QCOMPARE(resized.scaleFactors(), QSizeF(1.0, 1.0));
}

void TestViewport::tlsPolicyDefaults()
{
    // Verification is on unless the user explicitly chooses otherwise.
    const TlsPolicy policy;
    QCOMPARE(int(policy.mode()), int(TlsPolicy::Mode::VerifyPeer));
    QVERIFY(!policy.isInsecure());
    QVERIFY(policy.warningText().isEmpty());
    QCOMPARE(policy.settingsValue(), QStringLiteral("verify"));

    const QSslConfiguration configuration = policy.configurationFor();
    QCOMPARE(int(configuration.peerVerifyMode()), int(QSslSocket::VerifyPeer));
    QVERIFY(configuration.caCertificates().size() > 0);
    // The certificate host name is applied at socket level, not in the config.
    // Use the actual endpoint name rather than the reference's literal name.
    QSslSocket socket;
    socket.setSslConfiguration(configuration);
    socket.setPeerVerifyName(QStringLiteral("functions3.ultraviewer.net"));
    QCOMPARE(socket.peerVerifyName(), QStringLiteral("functions3.ultraviewer.net"));
}

void TestViewport::tlsPolicyIgnoresOnlyHostNameMismatch()
{
    const TlsPolicy policy(TlsPolicy::Mode::VerifyPeerAllowHostMismatch);
    QVERIFY(!policy.isInsecure());
    QVERIFY(policy.warningText().contains(QStringLiteral("host name")));
    QCOMPARE(policy.settingsValue(), QStringLiteral("verify-allow-hostname-mismatch"));

    const QList<QSslError> errors{QSslError(QSslError::HostNameMismatch), QSslError(QSslError::SelfSignedCertificate),
                                  QSslError(QSslError::CertificateExpired)};
    const QList<QSslError> ignorable = policy.ignorableErrors(errors);
    QCOMPARE(ignorable.size(), 1);
    QCOMPARE(int(ignorable.first().error()), int(QSslError::HostNameMismatch));
    // Full verification may never ignore anything.
    QVERIFY(TlsPolicy(TlsPolicy::Mode::VerifyPeer).ignorableErrors(errors).isEmpty());
}

void TestViewport::tlsPolicyAcceptAny()
{
    const TlsPolicy policy(TlsPolicy::Mode::AcceptAnyCertificate);
    QVERIFY(policy.isInsecure());
    QVERIFY(policy.warningText().contains(QStringLiteral("DISABLED")));
    QVERIFY(policy.warningText().contains(QStringLiteral("keystrokes"), Qt::CaseInsensitive));
    QCOMPARE(policy.settingsValue(), QStringLiteral("accept-any"));
    const QSslConfiguration configuration = policy.configurationFor();
    QCOMPARE(int(configuration.peerVerifyMode()), int(QSslSocket::VerifyNone));
    const QList<QSslError> errors{QSslError(QSslError::SelfSignedCertificate),
                                  QSslError(QSslError::CertificateExpired)};
    QCOMPARE(policy.ignorableErrors(errors).size(), 2);
}

void TestViewport::tlsPolicySettingsRoundTrip()
{
    for (const TlsPolicy::Mode mode : {TlsPolicy::Mode::VerifyPeer,
                                      TlsPolicy::Mode::VerifyPeerAllowHostMismatch,
                                      TlsPolicy::Mode::AcceptAnyCertificate}) {
        const TlsPolicy policy(mode);
        const TlsPolicy restored = TlsPolicy::fromSettingsValue(policy.settingsValue());
        QCOMPARE(int(restored.mode()), int(mode));
    }
    // An unknown value falls back to full verification instead of relaxing it.
    QCOMPARE(int(TlsPolicy::fromSettingsValue(QStringLiteral("nonsense")).mode()),
             int(TlsPolicy::Mode::VerifyPeer));
    QCOMPARE(int(TlsPolicy::fromSettingsValue(QString()).mode()), int(TlsPolicy::Mode::VerifyPeer));
}

void TestViewport::tlsPolicyAdditionalCa()
{
    TlsPolicy policy;
    policy.setAdditionalCaCertificateFile(uvtest::Fixtures::instance().tlsCertificatePath());
    const QSslConfiguration configuration = policy.configurationFor();
    QVERIFY(configuration.caCertificates().size() > 0);
    QCOMPARE(policy.additionalCaCertificateFile(),
             uvtest::Fixtures::instance().tlsCertificatePath());
    // A missing bundle must fail loudly, never silently skip the extra CA.
    TlsPolicy broken;
    broken.setAdditionalCaCertificateFile(QStringLiteral("/nonexistent/ca.pem"));
    try {
        broken.configurationFor();
        QFAIL("a missing CA bundle was accepted");
    } catch (const UvException &exception) {
        QCOMPARE(exception.code(), ErrorCode::Tls);
    }
    // Certificate description for the diagnostics panel must never be empty.
    const QSslCertificate certificate =
        QSslCertificate::fromPath(uvtest::Fixtures::instance().tlsCertificatePath()).value(0);
    QVERIFY(!certificate.isNull());
    QVERIFY(TlsPolicy::describePeerCertificate(certificate).contains(QStringLiteral("CN=ultraviewer")));
    QCOMPARE(TlsPolicy::describePeerCertificate(QSslCertificate()), QStringLiteral("no certificate"));
}

void TestViewport::sessionStateNames()
{
    // The state machine has nine states; the UI shows these names, and only the
    // active/terminal classification drives timeouts and buttons.
    QCOMPARE(kSessionStateCount, 9);
    QVERIFY(sessionStateIsActive(SessionState::Connecting));
    QVERIFY(sessionStateIsActive(SessionState::Connected));
    QVERIFY(!sessionStateIsActive(SessionState::Disconnected));
    QVERIFY(sessionStateIsTerminal(SessionState::Failed));
    QVERIFY(sessionStateIsTerminal(SessionState::Disconnected));
    QVERIFY(!sessionStateIsTerminal(SessionState::Connected));
    QVERIFY(!sessionStateName(SessionState::Connected).isEmpty());
    QVERIFY(!errorCodeDescription(ErrorCode::UnsupportedOperation).isEmpty());
    QVERIFY(!errorCodeName(ErrorCode::UnsupportedOperation).isEmpty());
    // Every state has a distinct name (used as a QML binding value).
    QSet<QString> names;
    for (int state = 0; state < kSessionStateCount; ++state)
        names.insert(sessionStateName(static_cast<SessionState>(state)));
    QCOMPARE(names.size(), kSessionStateCount);
}

QTEST_MAIN(TestViewport)
#include "tst_viewport.moc"
