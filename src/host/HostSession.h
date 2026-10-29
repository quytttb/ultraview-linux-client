#pragma once
//
// Host viewer state machine.
//
// The class performs no I/O: the network manager feeds it decoded relay
// payloads and captured frames, and consumes the returned actions.  That keeps
// login throttling, approval deadlines, frame pacing and input gating testable
// without a desktop or a relay.
//
#include <QByteArray>
#include <QList>
#include <QString>

#include "host/HostProtocol.h"

#include <cstdint>

namespace ultraview::host {

enum class HostPhase { None, Approval, Starting, Streaming };

QString hostPhaseName(HostPhase phase);

struct HostPeerInfo {
    QString viewerId;
    QString displayName;
    QString address;
    quint16 port = 0;

    bool isValid() const { return !viewerId.isEmpty() && port != 0; }
};

struct HostAction {
    enum class Type {
        SendPeer,    ///< send payload to address:port over the current relay
        Status,      ///< non-secret status text for the UI/log
        Prompt,      ///< a verified viewer is waiting for local approval
        Connected,   ///< the viewer acknowledged initialization and is streaming
        Input,       ///< inject this event into the local desktop
        ReleaseInput,///< release every key/button held for the viewer
        Ended,       ///< the viewer session ended; text is the reason
    };

    Type type = Type::Status;
    QString address;
    quint16 port = 0;
    QByteArray payload;
    QString text;
    QString viewerId;
    QString viewerName;
    HostInputEvent input;

    static HostAction sendPeer(const QString &address, quint16 port, const QByteArray &payload);
    static HostAction status(const QString &text);
    static HostAction prompt(const QString &viewerId, const QString &viewerName);
    static HostAction connected(const QString &viewerId, const QString &viewerName);
    static HostAction inputEvent(const HostInputEvent &event);
    static HostAction releaseInput();
    static HostAction ended(const QString &reason);
};

class HostSession {
public:
    explicit HostSession(QString password, QString customProof = {});

    void reset();

    HostPhase phase() const { return m_phase; }
    bool hasViewer() const { return !m_peer.viewerId.isEmpty(); }
    bool awaitingApproval() const { return m_phase == HostPhase::Approval; }
    bool streaming() const { return m_phase == HostPhase::Streaming; }
    bool controlAllowed() const { return m_control; }
    HostPeerInfo peer() const { return m_peer; }

    /// Consume one decoded payload from the viewer's relay route.
    void processPeer(const QByteArray &payload, const QString &address, quint16 port,
                     qint64 nowMs);

    /// Local decision for the pending request.  The first captured frame is
    /// kept until the viewer acknowledges command 0.
    bool approve(const HostFrame &firstFrame, bool allowControl, qint64 nowMs,
                 QString *error = nullptr);
    void decline();
    /// End the current viewer because the relay/capture/backend failed or the
    /// local user stopped sharing.  Keeps the pending command actions.
    void abortViewer(const QString &reason);
    void setControlAllowed(bool allowed);

    /// True once when the next capture should be taken.
    bool takeCaptureDue(qint64 nowMs);
    /// Submit a capture after takeCaptureDue().  Identical frames are paced
    /// slower, and a layout change re-sends initialization without resetting
    /// the frame number.
    bool submitFrame(const HostFrame &frame, qint64 nowMs, QString *error = nullptr);

    /// Expire approval, initialization and frame-acknowledgment deadlines.
    void tick(qint64 nowMs);

    /// Earliest deadline/capture time, or -1.
    qint64 nextWakeMs() const;

    QList<HostAction> takeActions();
    quint64 framesSent() const { return m_framesSent; }

private:
    void onLogin(const QByteArray &payload, const QString &address, quint16 port,
                 qint64 nowMs);
    bool initialize(const HostFrame &frame, bool allowControl, bool resetSequence,
                    qint64 nowMs, QString *error);
    bool sendFrame(const HostFrame &frame, qint64 nowMs, QString *error);
    void sendControlPermission();
    void endViewer(const QString &reason);

    QString m_password;
    QString m_customProof;
    LoginThrottle m_throttle;
    HostPeerInfo m_peer;
    QString m_peerConnectionHash;
    HostPhase m_phase = HostPhase::None;
    bool m_control = false;
    HostFrame m_pendingFrame;
    bool m_hasPendingFrame = false;
    HostFrame m_lastFrame;
    bool m_hasLastFrame = false;
    qint32 m_nextSequence = kFirstFrameNumber;
    bool m_waitingAck = false;
    qint64 m_captureAtMs = -1;
    qint64 m_deadlineMs = -1;
    quint64 m_framesSent = 0;
    QList<HostAction> m_actions;
};

} // namespace ultraview::host
