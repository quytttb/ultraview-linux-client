#include "host/HostManager.h"

#include "host/HostBackend.h"
#include "host/HostProfile.h"
#include "host/HostSession.h"
#include "network/RegistrationTransport.h"
#include "network/RelayTransport.h"
#include "protocol/PacketCodec.h"

#include <QDateTime>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace ultraview::host {
namespace {

constexpr int kCommandReplyTimeoutMs = 20 * 1000;
constexpr int kRelayAddressTimeoutMs = 15 * 1000;
constexpr int kKeepaliveIntervalMs = 60 * 1000;
constexpr int kTickIntervalMs = 20;

QString registrationEndpoint(const HostStartRequest &request)
{
    return request.registrationHost.isEmpty() ? QStringLiteral("functions3.ultraviewer.net")
                                              : request.registrationHost;
}

QString registrationPeerName(const HostStartRequest &request)
{
    if (!request.registrationPeerName.isEmpty())
        return request.registrationPeerName;
    return registrationEndpoint(request);
}

bool sameRelayRequest(const RelayRequest &left, const RelayRequest &right)
{
    return left.address == right.address && left.hostname == right.hostname
        && left.tag == right.tag && left.hash == right.hash;
}

} // namespace

QString hostStateName(HostState state)
{
    switch (state) {
    case HostState::Stopped:
        return QStringLiteral("Not hosting");
    case HostState::Starting:
        return QStringLiteral("Preparing screen sharing");
    case HostState::Registering:
        return QStringLiteral("Registering");
    case HostState::Waiting:
        return QStringLiteral("Waiting for a viewer");
    case HostState::RelayConnecting:
        return QStringLiteral("Preparing incoming connection");
    case HostState::WaitingApproval:
        return QStringLiteral("Waiting for local approval");
    case HostState::ViewerStarting:
        return QStringLiteral("Starting viewer");
    case HostState::Sharing:
        return QStringLiteral("Sharing");
    case HostState::Failed:
        return QStringLiteral("Host mode failed");
    }
    return QStringLiteral("Unknown");
}

HostManager::HostManager(QObject *parent)
    : QObject(parent)
{
    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(kTickIntervalMs);
    m_tickTimer->setTimerType(Qt::CoarseTimer);
    connect(m_tickTimer, &QTimer::timeout, this, &HostManager::tick);

    m_replyTimer = new QTimer(this);
    m_replyTimer->setSingleShot(true);
    connect(m_replyTimer, &QTimer::timeout, this, [this] {
        if (!m_registered)
            fail(QStringLiteral("UltraViewer did not assign an ID in time. Check the network "
                                "connection and try again."));
    });

    m_relayAddressTimer = new QTimer(this);
    m_relayAddressTimer->setSingleShot(true);
    connect(m_relayAddressTimer, &QTimer::timeout, this, [this] {
        if (m_relay != nullptr && !m_hasRelayAddress) {
            endRelay(QStringLiteral("The incoming relay did not provide an address. Ask Windows "
                                    "to connect again."));
        }
    });
}

HostManager::~HostManager()
{
    m_terminating = true;
    cleanup(QStringLiteral("The host stopped sharing."));
}

void HostManager::start(const HostStartRequest &request)
{
    if (m_state != HostState::Stopped && m_state != HostState::Failed)
        return;
    if (m_backend != nullptr || m_registration != nullptr) {
        // A previous failed run might still be releasing its objects.
        cleanup(QStringLiteral("The host stopped sharing."));
    }
    m_request = request;
    m_terminating = false;
    m_registered = false;
    m_hasRelayRequest = false;
    m_hasRelayAddress = false;
    m_relayRequest = {};
    m_relayAddress = {};
    m_registrationReply = {};
    m_nextKeepaliveMs = 0;

    if (!m_request.identity.isValid()) {
        fail(QStringLiteral("The host profile is incomplete. The host needs a stable identity, "
                            "an 8-digit password and an active IPv4 connection."));
        return;
    }
    if (m_request.timeoutMs < 1000)
        m_request.timeoutMs = 8000;

    setState(HostState::Starting);
    setStatus(QStringLiteral("Requesting screen-sharing permission from the desktop…"));

    QString error;
    m_backend = createHostBackend(&error);
    if (!m_backend) {
        fail(QStringLiteral("Screen sharing is unavailable: %1").arg(error));
        return;
    }
    m_backendName = m_backend->name();
    if (!m_backend->start(&error)) {
        m_backend.reset();
        fail(QStringLiteral("Screen sharing could not start: %1").arg(error));
        return;
    }
    if (m_terminating)
        return;

    const QVector<HostRect> displays = m_backend->displays(&error);
    if (displays.isEmpty()) {
        fail(QStringLiteral("The shared desktop reported no displays: %1").arg(error));
        return;
    }
    const auto layout = HostLayout::build(displays, &error);
    if (!layout.has_value()) {
        fail(QStringLiteral("The display layout cannot be shared: %1").arg(error));
        return;
    }
    m_layout = *layout;
    emit backendChanged(m_backendName,
                        QStringLiteral("%1 × %2").arg(m_layout.width).arg(m_layout.height));

    m_session = std::make_unique<HostSession>(m_request.identity.password, QString());
    m_registration = new network::RegistrationTransport(this);
    auto *registration = m_registration;
    connect(registration, &network::RegistrationTransport::ready, this, [this, registration] {
        if (registration == m_registration && !m_terminating)
            onRegistrationReady();
    });
    connect(registration, &network::RegistrationTransport::payload, this,
            [this, registration](const QByteArray &payload) {
                if (registration == m_registration && !m_terminating)
                    onRegistrationPayload(payload);
            });
    connect(registration, &network::RegistrationTransport::failed, this,
            [this, registration](const ErrorInfo &failure) {
                if (registration == m_registration && !m_terminating)
                    onRegistrationFailed(failure);
            });
    connect(registration, &network::RegistrationTransport::closed, this, [this, registration] {
        if (registration == m_registration && !m_terminating)
            onRegistrationClosed();
    });

    network::RegistrationTransport::Options options;
    options.host = registrationEndpoint(m_request);
    options.port = m_request.registrationPort != 0 ? m_request.registrationPort : 443;
    options.timeoutMs = m_request.timeoutMs;
    options.maxFrameBytes = m_request.maxFrameBytes;
    options.tlsPolicy = m_request.tlsPolicy;
    options.httpHostHeader = m_request.registrationHttpHost.isEmpty()
        ? options.host
        : m_request.registrationHttpHost;
    options.peerNameOverride = m_request.registrationPeerName;
    registration->setOptions(options);

    setState(HostState::Registering);
    setStatus(QStringLiteral("Registering this computer with UltraViewer…"));
    registration->start();
}

void HostManager::stop()
{
    if (m_state == HostState::Stopped)
        return;
    m_terminating = true;
    stopTimers();
    cleanup(QStringLiteral("The host stopped sharing."));
    m_terminating = false;
    setState(HostState::Stopped);
    setStatus(QStringLiteral("Not hosting"));
    emit stopped();
}

void HostManager::approve(bool allowControl)
{
    if (m_session == nullptr || !m_session->awaitingApproval())
        return;
    QString error;
    const auto frame = captureHostFrame(*m_backend, m_layout, &error);
    if (!frame.has_value()) {
        m_session->decline();
        flushActions();
        setStatus(QStringLiteral("The screen could not be captured; the request was refused. %1")
                      .arg(error));
        emit approvalCleared();
        setState(HostState::Waiting);
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!m_session->approve(*frame, allowControl, now, &error)) {
        m_session->decline();
        flushActions();
        setStatus(QStringLiteral("The viewer session could not start: %1").arg(error));
        emit approvalCleared();
        setState(HostState::Waiting);
        return;
    }
    emit approvalCleared();
    setState(HostState::ViewerStarting);
    setStatus(QStringLiteral("Sending the first screen frame to the viewer…"));
    flushActions();
}

void HostManager::decline()
{
    if (m_session == nullptr || !m_session->awaitingApproval())
        return;
    m_session->decline();
    emit approvalCleared();
    flushActions();
    setState(HostState::Waiting);
}

void HostManager::setControlAllowed(bool allowed)
{
    if (m_session == nullptr || !m_session->hasViewer())
        return;
    m_session->setControlAllowed(allowed);
    flushActions();
    emit controlChanged(m_session->controlAllowed());
}

void HostManager::tick()
{
    if (m_terminating || (m_state == HostState::Stopped || m_state == HostState::Failed))
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_session != nullptr) {
        m_session->tick(now);
        flushActions();
    }
    if (m_session != nullptr && m_session->streaming() && m_session->takeCaptureDue(now)) {
        QString error;
        const auto frame = captureHostFrame(*m_backend, m_layout, &error);
        if (!frame.has_value()) {
            m_session->abortViewer(QStringLiteral("Screen capture failed: %1").arg(error));
            flushActions();
            setStatus(QStringLiteral("Screen capture failed: %1").arg(error));
        } else if (!m_session->submitFrame(*frame, now, &error)) {
            m_session->abortViewer(QStringLiteral("Screen encoding failed: %1").arg(error));
            flushActions();
            setStatus(QStringLiteral("Screen encoding failed: %1").arg(error));
        }
        flushActions();
    }
    if (m_registered && m_nextKeepaliveMs > 0 && now >= m_nextKeepaliveMs) {
        m_nextKeepaliveMs = now + kKeepaliveIntervalMs;
        if (m_registration != nullptr && m_registration->isReady())
            m_registration->sendCommand(QByteArrayLiteral("2"));
        if (m_relay != nullptr && m_relay->isReady())
            m_relay->sendRelayKeepalive();
    }
}

void HostManager::flushActions(bool allowNetwork)
{
    if (m_session == nullptr)
        return;
    const QList<HostAction> actions = m_session->takeActions();
    for (const HostAction &action : actions) {
        switch (action.type) {
        case HostAction::Type::SendPeer:
            if (allowNetwork && m_relay != nullptr && m_relay->isReady()
                && !action.address.isEmpty() && action.port != 0) {
                m_relay->sendPayloadTo(action.address, action.port, action.payload, 0);
            }
            break;
        case HostAction::Type::Status:
            setStatus(action.text);
            break;
        case HostAction::Type::Prompt:
            setState(HostState::WaitingApproval);
            setStatus(QStringLiteral("A Windows viewer is requesting access. Approve or refuse "
                                     "the connection on this computer."));
            emit approvalRequested(action.viewerId, action.viewerName);
            break;
        case HostAction::Type::Connected:
            setState(HostState::Sharing);
            setStatus(QStringLiteral("Sharing the screen with %1.")
                          .arg(action.viewerName.isEmpty() ? QStringLiteral("the Windows viewer")
                                                          : action.viewerName));
            emit viewerConnected(action.viewerId, action.viewerName);
            emit controlChanged(m_session->controlAllowed());
            break;
        case HostAction::Type::Input:
            if (m_backend != nullptr && m_layout.isValid())
                m_backend->inject(action.input, m_layout);
            break;
        case HostAction::Type::ReleaseInput:
            if (m_backend != nullptr)
                m_backend->releaseAll();
            break;
        case HostAction::Type::Ended:
            if (m_backend != nullptr)
                m_backend->releaseAll();
            if (m_registered)
                setState(HostState::Waiting);
            setStatus(action.text);
            emit controlChanged(false);
            emit viewerDisconnected(action.text);
            break;
        }
    }
}

void HostManager::onRegistrationReady()
{
    if (m_registration == nullptr || m_terminating)
        return;
    QByteArray command;
    try {
        command = buildRegistrationCommand(m_request.identity);
    } catch (const UvException &exception) {
        fail(exception.message(), exception.detail());
        return;
    }
    m_registration->sendCommand(command);
    if (m_terminating || m_registration == nullptr)
        return;
    m_replyTimer->start(kCommandReplyTimeoutMs);
    setStatus(QStringLiteral("Waiting for UltraViewer to assign this computer an ID…"));
}

void HostManager::onRegistrationPayload(const QByteArray &payload)
{
    if (m_terminating)
        return;
    if (!m_registered && payload.startsWith("3:")) {
        try {
            m_registrationReply = parseRegistrationReply(payload);
        } catch (const UvException &exception) {
            fail(QStringLiteral("UltraViewer sent an invalid registration reply."),
                 exception.detail());
            return;
        }
        m_registered = true;
        m_replyTimer->stop();
        emit registered(m_registrationReply.id, m_registrationReply.publicIp);
        setState(HostState::Waiting);
        setStatus(QStringLiteral("Waiting for a Windows viewer. This computer is registered."));
        m_nextKeepaliveMs = QDateTime::currentMSecsSinceEpoch() + kKeepaliveIntervalMs;
        m_tickTimer->start();
        return;
    }
    if (!m_registered)
        return;

    if (payload.startsWith("11:") || payload.startsWith("17:")) {
        const auto request = parseRelayRequest(payload, m_registrationReply.pingList,
                                               m_registrationReply.pingHostNames);
        if (!request.has_value()) {
            setStatus(QStringLiteral("Ignored a malformed incoming connection request."));
            return;
        }
        if (m_hasRelayRequest && sameRelayRequest(*request, m_relayRequest)
            && m_relay != nullptr) {
            if (m_hasRelayAddress && m_registration != nullptr && m_registration->isReady()) {
                m_registration->sendCommand(relayAvailability(m_relayRequest, m_relayAddress));
            }
            return;
        }
        if (m_session != nullptr && m_session->hasViewer()) {
            setStatus(QStringLiteral("Another connection request arrived while a viewer is "
                                     "connected; it was ignored."));
            return;
        }
        startRelay(*request);
    }
}

void HostManager::onRegistrationFailed(const ErrorInfo &error)
{
    if (m_terminating)
        return;
    fail(error.message, error.detail);
}

void HostManager::onRegistrationClosed()
{
    if (m_terminating)
        return;
    fail(QStringLiteral("The UltraViewer command channel closed. Host mode stopped."));
}

void HostManager::startRelay(const RelayRequest &request)
{
    if (m_relay != nullptr) {
        auto *old = m_relay;
        m_relay = nullptr;
        old->stop();
        old->deleteLater();
    }
    m_hasRelayRequest = true;
    m_relayRequest = request;
    m_hasRelayAddress = false;
    m_relayAddress = {};

    m_relay = new network::RelayTransport(this);
    auto *relay = m_relay;
    connect(relay, &network::RelayTransport::ready, this, [this, relay] {
        if (relay == m_relay && !m_terminating)
            onRelayReady();
    });
    connect(relay, &network::RelayTransport::payloadFrom, this,
            [this, relay](const QByteArray &payload, quint32 addressValue, quint16 port,
                          bool unicode) {
                if (relay == m_relay && !m_terminating)
                    onRelayPayload(payload, addressValue, port, unicode);
            });
    connect(relay, &network::RelayTransport::systemPayload, this,
            [this, relay](const QByteArray &payload) {
                if (relay == m_relay && !m_terminating)
                    onRelaySystem(payload);
            });
    connect(relay, &network::RelayTransport::failed, this,
            [this, relay](const ErrorInfo &failure) {
                if (relay == m_relay && !m_terminating)
                    onRelayFailed(failure);
            });
    connect(relay, &network::RelayTransport::closed, this, [this, relay] {
        if (relay == m_relay && !m_terminating)
            onRelayClosed();
    });

    network::RelayTransport::Endpoint endpoint;
    endpoint.host = m_request.relayHostOverride.isEmpty() ? request.address
                                                          : m_request.relayHostOverride;
    endpoint.port = m_request.relayPortOverride != 0 ? m_request.relayPortOverride : 443;
    endpoint.httpHostHeader = m_request.relayHttpHostOverride.isEmpty()
        ? request.hostname
        : m_request.relayHttpHostOverride;
    if (!m_request.relayPeerName.isEmpty())
        endpoint.peerNameOverride = m_request.relayPeerName;
    else if (!m_request.relayHostOverride.isEmpty())
        endpoint.peerNameOverride = endpoint.host;
    else
        endpoint.peerNameOverride = registrationPeerName(m_request);
    endpoint.targetIpv4 = request.address;
    endpoint.targetPort = 0;
    endpoint.allowSystemMessages = true;

    security::TlsPolicy policy = m_request.tlsPolicy;
    if (m_registration != nullptr) {
        const QList<QSslCertificate> chain = m_registration->peerCertificateChain();
        if (chain.size() > 1)
            policy.addCaCertificates(chain.mid(1));
    }

    setState(HostState::RelayConnecting);
    setStatus(QStringLiteral("Opening the incoming UltraViewer relay…"));
    m_relayAddressTimer->start(kRelayAddressTimeoutMs);
    relay->start(endpoint, policy, m_request.timeoutMs, m_request.maxFrameBytes);
}

void HostManager::onRelayReady()
{
    if (m_relay == nullptr || !m_relay->isReady() || m_terminating)
        return;
    m_relay->sendSystemPayload(QByteArrayLiteral("0"), false);
    setStatus(QStringLiteral("Waiting for the relay to provide the incoming route…"));
}

void HostManager::onRelayPayload(const QByteArray &payload, quint32 addressValue, quint16 port,
                                 bool unicode)
{
    if (m_session == nullptr || m_terminating)
        return;
    const auto decoded = peerPayload(payload, unicode);
    if (!decoded.has_value()) {
        setStatus(QStringLiteral("Ignored an invalid viewer packet."));
        return;
    }
    if (addressValue == 0 || port == 0) {
        setStatus(QStringLiteral("Ignored a viewer packet with an invalid route."));
        return;
    }
    m_session->processPeer(*decoded, protocol::formatIpv4(addressValue), port,
                           QDateTime::currentMSecsSinceEpoch());
    flushActions();
}

void HostManager::onRelaySystem(const QByteArray &payload)
{
    if (!payload.startsWith("0:"))
        return;
    const auto address = parseRelayAddress(payload);
    if (!address.has_value()) {
        setStatus(QStringLiteral("The relay sent an invalid incoming address."));
        return;
    }
    m_hasRelayAddress = true;
    m_relayAddress = *address;
    m_relayAddressTimer->stop();
    if (m_registration != nullptr && m_registration->isReady())
        m_registration->sendCommand(relayAvailability(m_relayRequest, m_relayAddress));
    if (m_terminating)
        return;
    setStatus(QStringLiteral("Waiting for the Windows computer to sign in…"));
}

void HostManager::onRelayFailed(const ErrorInfo &error)
{
    if (m_terminating)
        return;
    endRelay(QStringLiteral("The incoming relay failed: %1").arg(error.message));
}

void HostManager::onRelayClosed()
{
    if (m_terminating)
        return;
    endRelay(QStringLiteral("The incoming relay disconnected."));
}

void HostManager::endRelay(const QString &reason)
{
    m_relayAddressTimer->stop();
    if (m_session != nullptr && m_session->hasViewer()) {
        m_session->abortViewer(reason);
        flushActions();
    }
    if (m_relay != nullptr) {
        auto *relay = m_relay;
        m_relay = nullptr;
        relay->stop();
        relay->deleteLater();
    }
    m_hasRelayRequest = false;
    m_hasRelayAddress = false;
    m_relayRequest = {};
    m_relayAddress = {};
    if (m_registered) {
        setState(HostState::Waiting);
        setStatus(reason + QStringLiteral(" Waiting for another connection."));
    } else {
        fail(reason);
    }
}

void HostManager::fail(const QString &message, const QString &detail)
{
    if (m_terminating)
        return;
    m_terminating = true;
    stopTimers();
    cleanup(QStringLiteral("The host stopped sharing."));
    m_terminating = false;
    setState(HostState::Failed);
    setStatus(message);
    emit failed(message, detail);
}

void HostManager::cleanup(const QString &reason)
{
    stopTimers();
    if (m_session != nullptr) {
        if (m_session->hasViewer()) {
            m_session->abortViewer(reason);
            flushActions();
        }
        m_session.reset();
    }
    if (m_backend != nullptr) {
        m_backend->releaseAll();
        m_backend->stop();
        m_backend.reset();
    }
    if (m_relay != nullptr) {
        auto *relay = m_relay;
        m_relay = nullptr;
        relay->stop();
        relay->deleteLater();
    }
    if (m_registration != nullptr) {
        auto *registration = m_registration;
        m_registration = nullptr;
        registration->stop();
        registration->deleteLater();
    }
    m_layout = {};
    m_backendName.clear();
    m_registered = false;
    m_hasRelayRequest = false;
    m_hasRelayAddress = false;
    m_registrationReply = {};
    m_relayRequest = {};
    m_relayAddress = {};
    m_nextKeepaliveMs = 0;
    m_request.clearSecrets();
}

void HostManager::stopTimers()
{
    if (m_tickTimer != nullptr)
        m_tickTimer->stop();
    if (m_replyTimer != nullptr)
        m_replyTimer->stop();
    if (m_relayAddressTimer != nullptr)
        m_relayAddressTimer->stop();
}

void HostManager::setState(HostState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(static_cast<int>(state), hostStateName(state));
}

void HostManager::setStatus(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || m_status == trimmed)
        return;
    m_status = trimmed;
    emit statusTextChanged(trimmed);
}

} // namespace ultraview::host
