#pragma once
//
// The control session: the native port of `control_session.ControlSession`.
//
// It owns the two channels and drives the recovered sequence:
//
//   registration endpoint                     partner relay endpoint
//   ---------------------                     ----------------------
//   TLS + HTTP preface                        TLS + HTTP preface
//   command 3 (InitMyPassword)                `login:` frame (UTF-16LE, flag 1)
//   <- server identity (client id)            <- kind 0 login acknowledgement
//   command 8 (connection request)            -> b"17"
//   <- status 1 route (or 2 / 11 / 99)        <- kind 1 screen updates -> b"18"
//                                             <- kind 3 size request  -> b"|||4<n>"
//
// Every step is timeout-guarded, every inbound byte is length-checked, and no
// failure path leaves a socket open.  The class lives in the session worker
// thread (see core/SessionManager) so the GUI thread never blocks on TLS.
//
// Deliberate, documented differences from the reference (docs/compatibility.md):
//   * audio packets (kinds 42/43/50/51) are reported as unsupported instead of
//     being decoded and discarded,
//   * unknown packet kinds raise an explicit "unsupported" notice instead of
//     being dropped by the viewer loop,
//   * BitBlt screen operations are refused by the decoder (it never silently
//     ignores a field it does not implement),
//   * b"18" is sent after the update has been decoded and published, which is
//     the same point in the sequence the reference uses (it acknowledges once the
//     renderer accepts the frame).
//
#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include "core/ErrorTypes.h"
#include "core/SessionState.h"
#include "decoder/ScreenDecoder.h"
#include "network/RegistrationTransport.h"
#include "network/RelayTransport.h"
#include "protocol/AnsiCodec.h"
#include "protocol/PacketCodec.h"
#include "protocol/RouteParser.h"
#include "security/TlsPolicy.h"

class QTimer;

namespace ultraview::network {

/// Everything the session needs.  Plain data so it can be posted to the worker
/// thread; the password is only held in memory and cleared on teardown.
struct SessionRequest {
    // --- registration channel identity (26-term command 3 schema) -----------
    QStringList registrationTerms;
    protocol::AnsiCodec codec = protocol::AnsiCodec::cp1252();

    // --- partner (17-term command 8 schema + the secret at login) -----------
    QStringList requestTerms;
    QString password;
    qint32 passwordType = 0;
    /// Overrides term 1 when not empty.
    QString partnerId;

    // --- local machine values ----------------------------------------------
    /// Host name placed in field 10 of the login schema; empty means "ask the
    /// system" (QSysInfo::machineHostName at start).
    QString hostName;
    /// Negotiation hint; the partner's real desktop size comes from the login
    /// acknowledgement.
    QSize viewportHint{1280, 720};

    // --- policy and limits --------------------------------------------------
    security::TlsPolicy tlsPolicy;
    int timeoutMs = 8000;
    quint32 maxFrameBytes = protocol::kDefaultMaxFrameBytes;
    /// Screen dictionary (the `8bit.dat` artifact).  Required for the RGB555
    /// path; without it the session still works for the JPEG/cache paths.
    QByteArray screenDictionary;
    decoder::ScreenDecoder::Options decoderOptions;

    // --- endpoint overrides (offline tests, private deployments) -----------
    /// Empty means the recovered production endpoint.
    QString registrationHost;
    quint16 registrationPort = 0;
    /// `Host:` header of the registration preface; empty means "the connect host".
    QString registrationHttpHost;
    /// Registration certificate name to verify; empty means registrationHost.
    /// The relay channel authenticates the same service name unless
    /// `relayPeerName` below is set.
    QString registrationPeerName;
    /// When set, replaces route.tcp_server_ip for the relay connection.
    QString relayHostOverride;
    /// When non-zero, replaces the relay port (443).
    quint16 relayPortOverride = 0;
    /// When set, overrides route.tcp_server_hostname as the `Host:` header of the
    /// relay tunnel preface. This HTTP header does not influence TLS identity.
    QString relayHttpHostOverride;
    /// Certificate name for the relay channel; empty means the registration
    /// certificate name (the reference wraps both channels with one TLS name).
    /// The route's hostname field is an HTTP `Host:` header, never a TLS name.
    QString relayPeerName;

    /// Structure check the transport layer cannot do.  Returns true when the
    /// request can be attempted; `error` receives a credential-free reason.
    bool validate(QString *error) const;

    /// Best-effort scrubbing of secret material.
    void clearSecrets();
};

/// Snapshot of counters and peer metadata for the diagnostics panel.
struct SessionStatistics {
    qint64 registrationBytesSent = 0;
    qint64 registrationBytesReceived = 0;
    qint64 relayBytesSent = 0;
    qint64 relayBytesReceived = 0;
    quint64 updatesDecoded = 0;
    quint64 framesPublished = 0;
    quint64 unsupportedNotices = 0;
    quint64 ignoredRegistrationPayloads = 0;
    quint64 sizeFollowUps = 0;
    quint64 scrollCopies = 0;
    quint64 rgb555Tiles = 0;
    quint64 atlasTiles = 0;
    quint64 cacheReferences = 0;
    quint64 cacheAdditions = 0;
    qint32 clientId = 0;
    QSize desktopSize;
    QString registrationPeer;
    QString relayPeer;
    QString relayProtocol;
    QString certificateSummary;
    QString tlsWarning;
    bool dictionaryInstalled = false;
    bool loginAcknowledged = false;
};

class ConnectionManager : public QObject {
    Q_OBJECT

public:
    /// Recovered production constants; the reference hard-codes both.
    static QString defaultRegistrationHost() { return QStringLiteral("functions3.ultraviewer.net"); }
    /// The relay endpoint is always port 443 in the reference.
    static constexpr quint16 kRelayPort = 443;
    /// Patches published to the render thread before it has to consume one.
    /// Beyond this the worker keeps accumulating dirty rectangles in the canvas
    /// instead of queueing patches (coalescing, never dropping pixels).
    static constexpr int kMaxInFlightPatches = 2;

    explicit ConnectionManager(QObject *parent = nullptr);
    ~ConnectionManager() override;

    SessionState state() const { return m_state; }
    const SessionRequest &request() const { return m_request; }
    protocol::TargetRoute route() const { return m_route; }
    QSize desktopSize() const { return m_desktopSize; }
    bool loginAcknowledged() const { return m_loginAcknowledged; }

    /// Only call from the session thread (tests do; the GUI uses the signal).
    SessionStatistics statistics() const;

    decoder::ScreenDecoder &decoder() { return m_decoder; }

public slots:
    /// Connect and run the session.  Emits a terminal signal (`failed` or
    /// `stopped`) for every call.
    void start(const ultraview::network::SessionRequest &request);
    /// Tear down.  Idempotent, safe from any state.
    void stop();
    /// Send a pre-built ASCII input command (see protocol/InputCodec).
    void sendInput(const QByteArray &payload);
    /// The renderer consumed `count` patches; the worker may publish again.
    void releaseFrames(int count);

signals:
    void stateChanged(ultraview::SessionState state);
    /// Terminal: the session cannot continue.
    void failed(const ultraview::ErrorInfo &error);
    /// Non-fatal, explicit report (unsupported packet kind, ignored data, ...).
    void notice(const ultraview::ErrorInfo &notice);
    /// Log-only diagnostic (e.g. the drained registration channel closing after
    /// the route); it does not replace the visible session status.
    void diagnostic(const QString &message);
    /// The partner desktop size is known and the canvas is allocated.
    void connected(const QSize &desktopSize);
    /// The command 8 reply carried a status 1 route; emitted before the relay
    /// channel is opened.
    void routeResolved(const ultraview::protocol::TargetRoute &route);
    /// One decoded, immutable screen patch.  Queued to the render thread.
    void frameReady(const ultraview::decoder::FrameUpdatePtr &update);
    void statisticsChanged(const ultraview::network::SessionStatistics &statistics);
    /// Terminal, graceful: stop() finished (never emitted after failed()).
    void stopped();

private:
    void setState(SessionState state);
    void fail(const ErrorInfo &error);
    void fail(const UvException &exception) { fail(exception.info()); }
    void note(ErrorCode code, const QString &message, const QString &detail = {});
    void armPhaseTimeout(int milliseconds);
    void disarmPhaseTimeout();

    void onRegistrationReady();
    void onRegistrationPayload(const QByteArray &payload);
    void onRegistrationFailed(const ErrorInfo &error);
    void onRegistrationClosed();

    void startRelay();
    void onRelayReady();
    void onRelayPayload(const QByteArray &payload);
    void onRelayFailed(const ErrorInfo &error);
    void onRelayClosed();

    void handleScreenUpdate(const QList<QByteArray> &fields);
    void handleSessionPacket(const QList<QByteArray> &fields);
    void publishStatistics(bool force);
    void sendRelayPayload(const QByteArray &payload, quint8 flag);
    void teardown();

    SessionRequest m_request;
    protocol::AnsiCodec m_codec = protocol::AnsiCodec::cp1252();
    SessionState m_state = SessionState::Disconnected;

    RegistrationTransport *m_registration = nullptr;
    RelayTransport *m_relay = nullptr;
    QTimer *m_phaseTimer = nullptr;
    QTimer *m_statisticsTimer = nullptr;

    protocol::TargetRoute m_route;
    qint32 m_clientId = 0;
    quint16 m_publicSourcePort = 0;
    QString m_publicIp;
    int m_registrationReplies = 0;
    bool m_loginAcknowledged = false;
    bool m_drainingRegistration = false;
    bool m_terminating = false;
    bool m_failureReported = false;
    QSize m_desktopSize;

    decoder::ScreenDecoder m_decoder;

    quint64 m_framesPublished = 0;
    quint64 m_unsupportedNotices = 0;
    quint64 m_ignoredRegistrationPayloads = 0;
    quint64 m_sizeFollowUps = 0;
    int m_inFlight = 0;

    SessionStatistics m_statisticsSnapshot;
    QElapsedTimer m_statisticsClock;
    qint64 m_lastStatisticsMs = -1;
};

} // namespace ultraview::network

Q_DECLARE_METATYPE(ultraview::network::SessionRequest)
Q_DECLARE_METATYPE(ultraview::network::SessionStatistics)
