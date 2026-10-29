#pragma once
//
// Host (sender) network worker.
//
// Owns the registration channel, the incoming relay, the desktop backend and the
// viewer state machine.  It runs on a worker thread; the QML-facing
// `HostViewModel` posts commands and mirrors the signals.
//
// Security boundaries:
//   * registration and relay TLS use the normal verified policy,
//   * the host password is kept in the profile/session only and never logged,
//   * every viewer is approved locally before a frame is sent,
//   * the relay request fields are untrusted until the relay address and the
//     login proof have been validated.
//
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

#include "core/ErrorTypes.h"
#include "host/HostBackend.h"
#include "host/HostProfile.h"
#include "host/HostProtocol.h"
#include "host/HostSession.h"
#include "protocol/PacketCodec.h"
#include "security/TlsPolicy.h"

class QTimer;

namespace ultraview::network {
class RegistrationTransport;
class RelayTransport;
}

namespace ultraview::host {

enum class HostState {
    Stopped,
    Starting,
    Registering,
    Waiting,
    RelayConnecting,
    WaitingApproval,
    ViewerStarting,
    Sharing,
    Failed,
};

QString hostStateName(HostState state);

struct HostStartRequest {
    HostIdentity identity;
    security::TlsPolicy tlsPolicy;
    int timeoutMs = 8000;
    quint32 maxFrameBytes = protocol::kDefaultMaxFrameBytes;

    /// Endpoint overrides (offline tests/private deployments); empty/default
    /// uses the recovered production values.
    QString registrationHost;
    quint16 registrationPort = 0;
    QString registrationHttpHost;
    QString registrationPeerName;
    QString relayHostOverride;
    quint16 relayPortOverride = 0;
    QString relayHttpHostOverride;
    QString relayPeerName;

    void clearSecrets() { identity = {}; }
};

class HostManager : public QObject {
    Q_OBJECT

public:
    explicit HostManager(QObject *parent = nullptr);
    ~HostManager() override;

    HostState state() const { return m_state; }

public slots:
    /// Register and serve viewers until stop().  Only one run is active.
    void start(const ultraview::host::HostStartRequest &request);
    void stop();
    /// Local approval for the currently pending, password-verified viewer.
    void approve(bool allowControl);
    void decline();
    void setControlAllowed(bool allowed);

signals:
    void stateChanged(int code, const QString &name);
    void statusTextChanged(const QString &text);
    void registered(const QString &id, const QString &publicAddress);
    void backendChanged(const QString &name, const QString &layout);
    void approvalRequested(const QString &viewerId, const QString &viewerName);
    void approvalCleared();
    void viewerConnected(const QString &viewerId, const QString &viewerName);
    void viewerDisconnected(const QString &reason);
    void controlChanged(bool allowed);
    void failed(const QString &message, const QString &detail);
    void stopped();

private:
    void setState(HostState state);
    void setStatus(const QString &text);
    void tick();
    void flushActions(bool allowNetwork = true);

    void onRegistrationReady();
    void onRegistrationPayload(const QByteArray &payload);
    void onRegistrationFailed(const ErrorInfo &error);
    void onRegistrationClosed();

    void startRelay(const RelayRequest &request);
    void onRelayReady();
    void onRelayPayload(const QByteArray &payload, quint32 addressValue, quint16 port,
                        bool unicode);
    void onRelaySystem(const QByteArray &payload);
    void onRelayFailed(const ErrorInfo &error);
    void onRelayClosed();
    void endRelay(const QString &reason);

    void fail(const QString &message, const QString &detail = {});
    void cleanup(const QString &reason);
    void stopTimers();

    HostStartRequest m_request;
    HostState m_state = HostState::Stopped;
    QString m_status;
    bool m_terminating = false;

    std::unique_ptr<HostBackend> m_backend;
    std::unique_ptr<HostSession> m_session;
    HostLayout m_layout;
    QString m_backendName;

    network::RegistrationTransport *m_registration = nullptr;
    network::RelayTransport *m_relay = nullptr;
    QTimer *m_tickTimer = nullptr;
    QTimer *m_replyTimer = nullptr;
    QTimer *m_relayAddressTimer = nullptr;

    RegistrationReply m_registrationReply;
    bool m_registered = false;
    bool m_hasRelayRequest = false;
    RelayRequest m_relayRequest;
    bool m_hasRelayAddress = false;
    RelayAddress m_relayAddress;
    qint64 m_nextKeepaliveMs = 0;
};

} // namespace ultraview::host
