#include "host/HostSession.h"

#include "core/ErrorTypes.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace ultraview::host {
namespace {

constexpr qint64 kApprovalTimeoutMs = 45 * 1000;
constexpr qint64 kInitializationTimeoutMs = 20 * 1000;
constexpr qint64 kChangedPacingMs = 40;
constexpr qint64 kUnchangedPacingMs = 250;
constexpr int kMaxViewerName = 50;

QString hostVersion()
{
    return QStringLiteral("6.6.133");
}

bool supportedVersion(const QString &version)
{
    const QStringList fields = version.split(QLatin1Char('.'));
    if (fields.size() != 3)
        return false;
    quint32 parts[3] = {0, 0, 0};
    for (int index = 0; index < 3; ++index) {
        const QString &field = fields.at(index);
        if (field.isEmpty())
            return false;
        quint64 value = 0;
        for (const QChar unit : field) {
            if (unit < QLatin1Char('0') || unit > QLatin1Char('9'))
                return false;
            value = value * 10 + static_cast<quint64>(unit.unicode() - '0');
            if (value > std::numeric_limits<quint32>::max())
                return false;
        }
        parts[index] = static_cast<quint32>(value);
    }
    return parts[0] >= 6;
}

QByteArray peerTag(const QByteArray &payload)
{
    const qsizetype separator = payload.indexOf('|');
    const QByteArray tag = separator < 0 ? payload : payload.left(separator);
    if (tag.isEmpty() || tag.size() > 3)
        return QByteArrayLiteral("other");
    for (const char unit : tag) {
        if (unit < '0' || unit > '9')
            return QByteArrayLiteral("other");
    }
    return tag;
}

bool isInputPayload(const QByteArray &payload)
{
    return payload.startsWith("8|||")
        || (payload.size() >= 4 && static_cast<uchar>(payload.at(0)) == 30
            && payload.at(1) == '|' && payload.at(2) == '|' && payload.at(3) == '|');
}

} // namespace

QString hostPhaseName(HostPhase phase)
{
    switch (phase) {
    case HostPhase::None:
        return QStringLiteral("Not hosting");
    case HostPhase::Approval:
        return QStringLiteral("Waiting for local approval");
    case HostPhase::Starting:
        return QStringLiteral("Viewer starting");
    case HostPhase::Streaming:
        return QStringLiteral("Sharing");
    }
    return QStringLiteral("Unknown");
}

HostAction HostAction::sendPeer(const QString &address, quint16 port, const QByteArray &payload)
{
    HostAction action;
    action.type = Type::SendPeer;
    action.address = address;
    action.port = port;
    action.payload = payload;
    return action;
}

HostAction HostAction::status(const QString &text)
{
    HostAction action;
    action.type = Type::Status;
    action.text = text;
    return action;
}

HostAction HostAction::prompt(const QString &viewerId, const QString &viewerName)
{
    HostAction action;
    action.type = Type::Prompt;
    action.viewerId = viewerId;
    action.viewerName = viewerName;
    return action;
}

HostAction HostAction::connected(const QString &viewerId, const QString &viewerName)
{
    HostAction action;
    action.type = Type::Connected;
    action.viewerId = viewerId;
    action.viewerName = viewerName;
    return action;
}

HostAction HostAction::inputEvent(const HostInputEvent &event)
{
    HostAction action;
    action.type = Type::Input;
    action.input = event;
    return action;
}

HostAction HostAction::releaseInput()
{
    HostAction action;
    action.type = Type::ReleaseInput;
    return action;
}

HostAction HostAction::ended(const QString &reason)
{
    HostAction action;
    action.type = Type::Ended;
    action.text = reason;
    return action;
}

HostSession::HostSession(QString password, QString customProof)
    : m_password(std::move(password))
    , m_customProof(std::move(customProof))
{
}

void HostSession::reset()
{
    m_peer = {};
    m_phase = HostPhase::None;
    m_control = false;
    m_pendingFrame = {};
    m_hasPendingFrame = false;
    m_lastFrame = {};
    m_hasLastFrame = false;
    m_nextSequence = kFirstFrameNumber;
    m_waitingAck = false;
    m_captureAtMs = -1;
    m_deadlineMs = -1;
    m_actions.clear();
}

void HostSession::processPeer(const QByteArray &payload, const QString &address, quint16 port,
                              qint64 nowMs)
{
    if (payload.startsWith("login:")) {
        onLogin(payload, address, port, nowMs);
        return;
    }
    if (!hasViewer())
        return;
    if (m_peer.address != address || m_peer.port != port)
        return;

    if (payload == QByteArrayLiteral("17") && m_phase == HostPhase::Starting) {
        m_deadlineMs = -1;
        m_phase = HostPhase::Streaming;
        m_actions.append(HostAction::connected(m_peer.viewerId, m_peer.displayName));
        if (m_hasPendingFrame) {
            const HostFrame frame = m_pendingFrame;
            m_hasPendingFrame = false;
            m_pendingFrame = {};
            QString error;
            if (!sendFrame(frame, nowMs, &error)) {
                endViewer(QStringLiteral("The host could not encode the first screen frame."));
            }
        }
        return;
    }

    const QByteArray tag = peerTag(payload);
    if (tag == QByteArrayLiteral("19")) {
        endViewer(QStringLiteral("The Windows computer disconnected."));
        return;
    }
    if (tag == QByteArrayLiteral("20")) {
        m_actions.append(HostAction::sendPeer(m_peer.address, m_peer.port, keepaliveReply()));
        return;
    }
    if (tag == QByteArrayLiteral("18") && m_phase == HostPhase::Streaming && m_waitingAck) {
        m_waitingAck = false;
        m_deadlineMs = -1;
        m_captureAtMs = nowMs + kChangedPacingMs;
        return;
    }
    if (payload == QByteArrayLiteral("32|||1")) {
        endViewer(QStringLiteral("The Windows viewer requested an unsupported screen color "
                                 "mode. Use 16-bit color."));
        return;
    }
    if (isInputPayload(payload)) {
        if (m_phase == HostPhase::Streaming && m_control && payload.size() <= 4096) {
            if (const auto event = decodeInput(payload); event.has_value())
                m_actions.append(HostAction::inputEvent(*event));
        }
        return;
    }
    // Chat, clipboard and file messages are not implemented.  Ignore unknown
    // tags rather than guessing their semantics.
}

void HostSession::onLogin(const QByteArray &payload, const QString &address, quint16 port,
                          qint64 nowMs)
{
    QString parseError;
    std::optional<LoginRequest> login;
    if (!m_throttle.isLocked(nowMs))
        login = parseLogin(payload, &parseError);

    bool verified = false;
    if (login.has_value()) {
        verified = verifyRandomPassword(*login, m_password);
        if (!verified && !m_customProof.isEmpty())
            verified = verifyCustomProof(*login, m_customProof);
    }
    if (!login.has_value() || !verified) {
        const bool locked = m_throttle.recordFailure(nowMs);
        m_actions.append(HostAction::sendPeer(address, port, singleCommand(22)));
        if (locked) {
            m_actions.append(HostAction::status(
                QStringLiteral("Too many wrong passwords. New connections are refused for 5 "
                               "minutes.")));
        }
        return;
    }
    m_throttle.recordSuccess();

    const QString hash = login->connectionHashKey();
    if (login->displayColors != 2 || !supportedVersion(login->clientVersion())
        || hash.isEmpty() || hash.size() > 128) {
        m_actions.append(HostAction::sendPeer(address, port, singleCommand(27)));
        m_actions.append(HostAction::status(
            QStringLiteral("The Windows viewer needs modern 16-bit screen mode and version "
                           "6.0.0 or newer.")));
        return;
    }

    if (hasViewer()) {
        if (m_peer.address == address && m_peer.port == port
            && m_peerConnectionHash == hash && m_phase == HostPhase::Streaming) {
            if (m_hasLastFrame) {
                QString error;
                if (!initialize(m_lastFrame, m_control, true, nowMs, &error)) {
                    endViewer(QStringLiteral("The host could not restart the viewer session."));
                }
            }
        } else if (m_peer.address != address || m_peer.port != port) {
            m_actions.append(HostAction::sendPeer(address, port, singleCommand(27)));
        }
        return;
    }

    m_peer.viewerId = login->claimedId();
    m_peer.displayName = login->displayName().left(kMaxViewerName);
    m_peer.address = address;
    m_peer.port = port;
    m_peerConnectionHash = hash;
    m_phase = HostPhase::Approval;
    m_deadlineMs = nowMs + kApprovalTimeoutMs;
    m_actions.append(HostAction::prompt(m_peer.viewerId, m_peer.displayName));
}

bool HostSession::approve(const HostFrame &firstFrame, bool allowControl, qint64 nowMs,
                          QString *error)
{
    if (m_phase != HostPhase::Approval) {
        if (error)
            *error = QStringLiteral("The incoming connection is no longer available.");
        return false;
    }
    return initialize(firstFrame, allowControl, true, nowMs, error);
}

void HostSession::decline()
{
    if (hasViewer())
        endViewer(QStringLiteral("The incoming request was declined."));
}

void HostSession::abortViewer(const QString &reason)
{
    if (hasViewer())
        endViewer(reason);
}

void HostSession::setControlAllowed(bool allowed)
{
    if (m_phase != HostPhase::Starting && m_phase != HostPhase::Streaming)
        return;
    m_control = allowed;
    if (!allowed)
        m_actions.append(HostAction::releaseInput());
    sendControlPermission();
}

bool HostSession::takeCaptureDue(qint64 nowMs)
{
    if (m_phase != HostPhase::Streaming || m_waitingAck || m_captureAtMs < 0
        || m_captureAtMs > nowMs) {
        return false;
    }
    m_captureAtMs = -1;
    return true;
}

bool HostSession::submitFrame(const HostFrame &frame, qint64 nowMs, QString *error)
{
    if (m_phase != HostPhase::Streaming || m_waitingAck)
        return true;
    if (m_hasLastFrame && m_lastFrame.sameLayout(frame)
        && m_lastFrame.rgb555BottomUp == frame.rgb555BottomUp) {
        m_captureAtMs = nowMs + kUnchangedPacingMs;
        return true;
    }
    return sendFrame(frame, nowMs, error);
}

void HostSession::tick(qint64 nowMs)
{
    if (hasViewer() && m_deadlineMs >= 0 && m_deadlineMs <= nowMs) {
        endViewer(QStringLiteral("The incoming connection timed out. Ask Windows to connect "
                                 "again."));
    }
}

qint64 HostSession::nextWakeMs() const
{
    qint64 result = -1;
    const auto consider = [&result](qint64 value) {
        if (value >= 0 && (result < 0 || value < result))
            result = value;
    };
    consider(m_deadlineMs);
    consider(m_captureAtMs);
    return result;
}

QList<HostAction> HostSession::takeActions()
{
    return std::exchange(m_actions, {});
}

bool HostSession::initialize(const HostFrame &frame, bool allowControl, bool resetSequence,
                             qint64 nowMs, QString *error)
{
    if (!frame.isValid()) {
        if (error)
            *error = QStringLiteral("The captured screen frame is invalid.");
        return false;
    }
    QByteArray init;
    try {
        init = initialization(frame.width, frame.height, hostVersion(), frame.monitors);
    } catch (const UvException &exception) {
        if (error)
            *error = exception.message();
        return false;
    }

    m_phase = HostPhase::Starting;
    m_control = allowControl;
    m_pendingFrame = frame;
    m_hasPendingFrame = true;
    m_lastFrame = {};
    m_hasLastFrame = false;
    if (resetSequence)
        m_nextSequence = kFirstFrameNumber;
    m_waitingAck = false;
    m_captureAtMs = -1;
    m_actions.append(HostAction::releaseInput());
    m_deadlineMs = nowMs + kInitializationTimeoutMs;
    m_actions.append(HostAction::sendPeer(m_peer.address, m_peer.port, init));
    sendControlPermission();
    return true;
}

bool HostSession::sendFrame(const HostFrame &frame, qint64 nowMs, QString *error)
{
    if (m_phase != HostPhase::Streaming || m_waitingAck)
        return true;
    if (m_hasLastFrame && !m_lastFrame.sameLayout(frame)) {
        return initialize(frame, m_control, false, nowMs, error);
    }
    if (m_nextSequence == std::numeric_limits<qint32>::max()) {
        if (error)
            *error = QStringLiteral("The screen frame number is exhausted.");
        return false;
    }
    QByteArray payload;
    try {
        payload = fullFrame(frame, m_nextSequence);
    } catch (const UvException &exception) {
        if (error)
            *error = exception.message();
        return false;
    }
    ++m_nextSequence;
    m_waitingAck = true;
    m_lastFrame = frame;
    m_hasLastFrame = true;
    m_deadlineMs = nowMs + kInitializationTimeoutMs;
    ++m_framesSent;
    m_actions.append(HostAction::sendPeer(m_peer.address, m_peer.port, payload));
    return true;
}

void HostSession::sendControlPermission()
{
    if (!hasViewer())
        return;
    m_actions.append(HostAction::sendPeer(m_peer.address, m_peer.port,
                                          controlPermission(m_control)));
}

void HostSession::endViewer(const QString &reason)
{
    const HostPeerInfo old = m_peer;
    m_peer = {};
    m_peerConnectionHash.clear();
    m_phase = HostPhase::None;
    m_control = false;
    m_waitingAck = false;
    m_captureAtMs = -1;
    m_deadlineMs = -1;
    m_hasPendingFrame = false;
    m_pendingFrame = {};
    m_hasLastFrame = false;
    m_lastFrame = {};
    m_actions.append(HostAction::releaseInput());
    if (!old.viewerId.isEmpty()) {
        m_actions.append(HostAction::ended(reason));
        m_actions.append(HostAction::sendPeer(old.address, old.port, singleCommand(27)));
    }
}

} // namespace ultraview::host
