#include "network/ConnectionManager.h"

#include "protocol/AuthenticationCodec.h"
#include "protocol/InputCodec.h"
#include "protocol/RegistrationCodec.h"

#include <QDateTime>
#include <QSysInfo>
#include <QTimer>

#include <algorithm>

namespace ultraview::network {
namespace {

/// Recovered status semantics of a command 8 refusal are pinned in RouteParser.
constexpr int kCommand8MaxReplies = 4;

/// Minimum interval between two statistics emissions; a coalescing timer
/// publishes the pending snapshot once the interval has passed.
constexpr int kStatisticsIntervalMs = 400;

QString defaultHostName()
{
    const QString host = QSysInfo::machineHostName();
    return host.isEmpty() ? QStringLiteral("linux") : host;
}

} // namespace

// ---------------------------------------------------------------------------
// SessionRequest
// ---------------------------------------------------------------------------

bool SessionRequest::validate(QString *error) const
{
    const auto reject = [error](const QString &text) {
        if (error != nullptr)
            *error = text;
        return false;
    };

    if (registrationTerms.size() != 26)
        return reject(QStringLiteral("the registration profile needs exactly 26 terms (got %1)")
                          .arg(registrationTerms.size()));
    const QString reason = protocol::RegistrationCodec::validateRegistrationTerms(registrationTerms);
    if (!reason.isEmpty())
        return reject(reason);

    if (requestTerms.size() != 17)
        return reject(QStringLiteral("the partner request template needs exactly 17 terms (got %1)")
                          .arg(requestTerms.size()));
    if (!requestTerms.at(16).isEmpty())
        return reject(QStringLiteral("P2P negotiation is not supported; request term 16 must be empty"));
    for (int index = 0; index < requestTerms.size(); ++index) {
        if (requestTerms.at(index).contains(QLatin1Char(':'))
            || requestTerms.at(index).contains(QChar(0))
            || requestTerms.at(index).contains(QLatin1Char('|'))) {
            return reject(QStringLiteral("request term %1 contains a reserved delimiter").arg(index));
        }
    }
    if (timeoutMs <= 0)
        return reject(QStringLiteral("the transport timeout must be positive"));
    if (maxFrameBytes == 0 || maxFrameBytes > protocol::kAbsoluteMaxFrameBytes)
        return reject(QStringLiteral("the frame limit must be between 1 and %1 bytes")
                          .arg(protocol::kAbsoluteMaxFrameBytes));
    if (!codec.canEncode(password) && passwordType == 0)
        return reject(QStringLiteral("the password cannot be encoded with the %1 code page").arg(codec.name()));
    return true;
}

void SessionRequest::clearSecrets()
{
    password.fill(QChar(0));
    password.clear();
    // The request terms may carry authentication material (term 2 is a computed
    // digest, but the recovered profile also stores values here); clear the ones
    // this client wrote.
    if (requestTerms.size() == 17) {
        requestTerms[2].fill(QChar(0));
        requestTerms[2].clear();
    }
}

// ---------------------------------------------------------------------------
// ConnectionManager
// ---------------------------------------------------------------------------

ConnectionManager::ConnectionManager(QObject *parent)
    : QObject(parent)
{
    m_phaseTimer = new QTimer(this);
    m_phaseTimer->setSingleShot(true);
    connect(m_phaseTimer, &QTimer::timeout, this, [this] {
        fail(ErrorInfo{ErrorCode::Timeout,
                       QStringLiteral("the connection timed out while %1").arg(sessionStateName(m_state)),
                       QStringLiteral("phase timeout after %1 ms").arg(m_request.timeoutMs)});
    });
    // Coalesces the rate-limited statistics updates: without it a counter that
    // changed during the throttle window would stay invisible until the next
    // unrelated event.
    m_statisticsTimer = new QTimer(this);
    m_statisticsTimer->setSingleShot(true);
    connect(m_statisticsTimer, &QTimer::timeout, this, [this] { publishStatistics(true); });
}

ConnectionManager::~ConnectionManager() = default;

SessionStatistics ConnectionManager::statistics() const
{
    return m_statisticsSnapshot;
}

void ConnectionManager::setState(SessionState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void ConnectionManager::armPhaseTimeout(int milliseconds)
{
    if (milliseconds > 0)
        m_phaseTimer->start(milliseconds);
}

void ConnectionManager::disarmPhaseTimeout()
{
    m_phaseTimer->stop();
}

void ConnectionManager::teardown()
{
    disarmPhaseTimeout();
    if (m_statisticsTimer != nullptr)
        m_statisticsTimer->stop();
    if (m_registration != nullptr) {
        m_registration->disconnect(this);
        m_registration->stop();
        m_registration->deleteLater();
        m_registration = nullptr;
    }
    if (m_relay != nullptr) {
        m_relay->disconnect(this);
        m_relay->stop();
        m_relay->deleteLater();
        m_relay = nullptr;
    }
}

void ConnectionManager::fail(const ErrorInfo &error)
{
    if (m_terminating || m_failureReported)
        return;
    m_failureReported = true;
    m_terminating = true;
    publishStatistics(true);
    teardown();
    setState(SessionState::Failed);
    emit failed(error);
}

void ConnectionManager::note(ErrorCode code, const QString &message, const QString &detail)
{
    ++m_unsupportedNotices;
    emit notice(ErrorInfo{code, message, detail});
}

void ConnectionManager::start(const ultraview::network::SessionRequest &request)
{
    // A running session is closed first, exactly like `ControlSession.connect`.
    if (sessionStateIsActive(m_state))
        teardown();

    m_terminating = false;
    m_failureReported = false;
    m_loginAcknowledged = false;
    m_drainingRegistration = false;
    m_registrationReplies = 0;
    m_clientId = 0;
    m_publicSourcePort = 0;
    m_publicIp.clear();
    m_route = protocol::TargetRoute{};
    m_desktopSize = QSize();
    m_framesPublished = 0;
    m_unsupportedNotices = 0;
    m_ignoredRegistrationPayloads = 0;
    m_sizeFollowUps = 0;
    m_inFlight = 0;
    m_statisticsSnapshot = SessionStatistics{};

    m_request = request;
    m_codec = request.codec;
    m_statisticsClock.start();
    m_lastStatisticsMs = -1;

    QString validationError;
    if (!m_request.validate(&validationError)) {
        setState(SessionState::Connecting);
        fail(ErrorInfo{ErrorCode::Configuration, QStringLiteral("the session request is invalid"),
                       validationError});
        return;
    }

    try {
        m_decoder.setOptions(request.decoderOptions);
        m_decoder.setDictionary(request.screenDictionary);
        m_decoder.resetCounters();
        m_decoder.cache().clear();
    } catch (const UvException &exception) {
        fail(exception);
        return;
    }
    if (!m_decoder.hasDictionary()) {
        note(ErrorCode::Configuration,
             QStringLiteral("no screen dictionary installed: compressed RGB555 updates cannot be decoded"),
             QStringLiteral("supply the 8bit.dat artifact in the settings to receive the full screen"));
    }

    m_registration = new RegistrationTransport(this);
    connect(m_registration, &RegistrationTransport::ready, this, &ConnectionManager::onRegistrationReady);
    connect(m_registration, &RegistrationTransport::payload, this, &ConnectionManager::onRegistrationPayload);
    connect(m_registration, &RegistrationTransport::failed, this, &ConnectionManager::onRegistrationFailed);
    connect(m_registration, &RegistrationTransport::closed, this, &ConnectionManager::onRegistrationClosed);

    RegistrationTransport::Options options;
    options.host = request.registrationHost.isEmpty() ? defaultRegistrationHost() : request.registrationHost;
    options.port = request.registrationPort != 0 ? request.registrationPort : 443;
    options.timeoutMs = request.timeoutMs;
    options.maxFrameBytes = request.maxFrameBytes;
    options.tlsPolicy = request.tlsPolicy;
    options.httpHostHeader = request.registrationHttpHost.isEmpty() ? options.host : request.registrationHttpHost;
    // Authenticate the actual service name. The reference's literal
    // "ultraviewer" does not match the production *.ultraviewer.net certificate.
    options.peerNameOverride = request.registrationPeerName;
    m_registration->setOptions(options);

    setState(SessionState::Connecting);
    armPhaseTimeout(request.timeoutMs * 2);
    publishStatistics(true);
    m_registration->start();
}

void ConnectionManager::stop()
{
    m_terminating = true;
    teardown();
    m_request.clearSecrets();
    setState(SessionState::Disconnected);
    publishStatistics(true);
    emit stopped();
}

void ConnectionManager::onRegistrationReady()
{
    if (m_terminating || m_registration == nullptr)
        return;
    QByteArray command;
    try {
        command = protocol::RegistrationCodec::buildRegistrationCommand(m_request.registrationTerms, m_codec);
    } catch (const UvException &exception) {
        fail(exception);
        return;
    }
    setState(SessionState::Registering);
    armPhaseTimeout(m_request.timeoutMs);
    publishStatistics(true);
    m_registration->sendCommand(command);
}

void ConnectionManager::onRegistrationPayload(const QByteArray &payload)
{
    if (m_terminating || m_registration == nullptr)
        return;

    if (m_state == SessionState::Registering) {
        protocol::Command3Identity identity;
        try {
            identity = protocol::RegistrationCodec::parseCommand3Identity(payload, m_codec);
        } catch (const UvException &exception) {
            fail(exception);
            return;
        }
        m_clientId = identity.clientId;
        m_publicSourcePort = identity.publicSourcePort;
        m_publicIp = identity.publicIp;

        QByteArray command;
        try {
            protocol::ConnectionRequestContext context;
            context.templateTerms = m_request.requestTerms;
            context.password = m_request.password;
            context.passwordType = m_request.passwordType;
            context.codec = m_codec;
            context.registrationTerm2 = m_request.registrationTerms.value(2);
            context.partnerId = m_request.partnerId;
            const QStringList terms = protocol::AuthenticationCodec::resolveConnectionRequestTerms(context, m_clientId);
            command = protocol::AuthenticationCodec::buildConnectionRequestCommand(terms, m_codec);
        } catch (const UvException &exception) {
            fail(exception);
            return;
        }
        m_registrationReplies = 0;
        setState(SessionState::RequestingPartner);
        armPhaseTimeout(m_request.timeoutMs);
        publishStatistics(true);
        m_registration->sendCommand(command); // may fail() synchronously; state is consistent
        return;
    }

    if (m_state == SessionState::RequestingPartner) {
        ++m_registrationReplies;
        if (m_registrationReplies > kCommand8MaxReplies) {
            fail(ErrorInfo{ErrorCode::Protocol,
                           QStringLiteral("the server answered the connection request more than four times")});
            return;
        }
        if (payload.startsWith("8:")) {
            try {
                const QString status = protocol::RouteParser::statusField(payload, m_codec);
                if (status != QLatin1String("1")) {
                    fail(protocol::RouteParser::authenticationError(status).info());
                    return;
                }
                m_route = protocol::RouteParser::parseRoute(payload, m_codec);
            } catch (const UvException &exception) {
                fail(exception);
                return;
            }
            startRelay();
            return;
        }
        if (m_registrationReplies >= kCommand8MaxReplies) {
            fail(ErrorInfo{ErrorCode::Protocol,
                           QStringLiteral("the server did not send route information"),
                           QStringLiteral("%1 replies without a command 8 route").arg(m_registrationReplies)});
            return;
        }
        // Another unrelated payload; consume it like the reference loop does and
        // keep waiting inside the same deadline.
        armPhaseTimeout(m_request.timeoutMs);
        return;
    }

    // Payload after the handshake: the registration channel is no longer part of
    // the control path, so it is drained and reported instead of interpreted.
    ++m_ignoredRegistrationPayloads;
    note(ErrorCode::UnsupportedOperation,
         QStringLiteral("unexpected payload on the registration channel"),
         QStringLiteral("%1 bytes after the handshake were ignored").arg(payload.size()));
}

void ConnectionManager::onRegistrationFailed(const ErrorInfo &error)
{
    if (m_terminating)
        return;
    // After the route has been resolved the registration channel is only drained;
    // losing it does not break the session.
    if (m_drainingRegistration) {
        ++m_ignoredRegistrationPayloads;
        note(ErrorCode::Network, QStringLiteral("the registration channel was lost"),
             error.summary());
        return;
    }
    fail(error);
}

void ConnectionManager::onRegistrationClosed()
{
    if (m_terminating)
        return;
    if (m_drainingRegistration) {
        note(ErrorCode::Network, QStringLiteral("the registration channel was closed by the server"));
        return;
    }
    fail(ErrorInfo{ErrorCode::Network, QStringLiteral("the registration channel was closed by the server")});
}

void ConnectionManager::startRelay()
{
    if (m_registration != nullptr) {
        // The reference stops reading this channel once the route is known; the
        // native client keeps the socket (so the server sees the same lifecycle)
        // but drains it without interpretation.
        m_registration->setDrainPayloads(true);
        m_drainingRegistration = true;
    }

    RelayTransport::Endpoint endpoint;
    endpoint.host = m_request.relayHostOverride.isEmpty() ? m_route.tcpServerIp : m_request.relayHostOverride;
    endpoint.port = m_request.relayPortOverride != 0 ? m_request.relayPortOverride : kRelayPort;
    endpoint.httpHostHeader = m_request.relayHttpHostOverride.isEmpty() ? m_route.tcpServerHostname
                                                                       : m_request.relayHttpHostOverride;
    // The route's hostname field is the HTTP `Host:` header of the tunnel preface
    // (the reference passes it only there); it is not a certificate name.  Both
    // channels are wrapped with the same recovered TLS name, so the relay
    // authenticates the service name the registration channel verified.  An
    // explicit relay certificate name wins, then an explicit connect-host
    // override (which authenticates that host).
    const QString registrationName = !m_request.registrationPeerName.isEmpty()
        ? m_request.registrationPeerName
        : (m_request.registrationHost.isEmpty() ? defaultRegistrationHost()
                                                : m_request.registrationHost);
    if (!m_request.relayPeerName.isEmpty())
        endpoint.peerNameOverride = m_request.relayPeerName;
    else if (!m_request.relayHostOverride.isEmpty())
        endpoint.peerNameOverride = endpoint.host;
    else
        endpoint.peerNameOverride = registrationName;
    endpoint.targetIpv4 = m_route.publicIp;
    endpoint.targetPort = m_route.tcpPort;

    if (endpoint.host.isEmpty()) {
        fail(ErrorInfo{ErrorCode::Protocol, QStringLiteral("the route does not contain a relay host")});
        return;
    }
    quint32 address = 0;
    if (!protocol::parseCanonicalIpv4(endpoint.targetIpv4, &address)) {
        fail(ErrorInfo{ErrorCode::Protocol,
                       QStringLiteral("the route does not contain a usable partner address"),
                       QStringLiteral("public endpoint field is not a dotted quad")});
        return;
    }

    m_relay = new RelayTransport(this);
    connect(m_relay, &RelayTransport::ready, this, &ConnectionManager::onRelayReady);
    connect(m_relay, &RelayTransport::payload, this, &ConnectionManager::onRelayPayload);
    connect(m_relay, &RelayTransport::failed, this, &ConnectionManager::onRelayFailed);
    connect(m_relay, &RelayTransport::closed, this, &ConnectionManager::onRelayClosed);

    setState(SessionState::RelayConnecting);
    armPhaseTimeout(m_request.timeoutMs * 2);
    emit routeResolved(m_route);
    publishStatistics(true);
    m_relay->start(endpoint, m_request.tlsPolicy, m_request.timeoutMs, m_request.maxFrameBytes);
}

void ConnectionManager::onRelayReady()
{
    if (m_terminating || m_relay == nullptr)
        return;

    QByteArray login;
    try {
        const QString hostName = m_request.hostName.isEmpty() ? defaultHostName() : m_request.hostName;
        const QStringList fields = protocol::AuthenticationCodec::defaultLoginFields(
            m_clientId, hostName, m_route.partnerSecretToken.value_or(QString()),
            m_request.viewportHint.width(), m_request.viewportHint.height());
        const std::optional<QString> salt =
            m_request.passwordType == 0
                ? std::optional<QString>(protocol::AuthenticationCodec::randomDigits(8))
                : std::nullopt;
        login = protocol::AuthenticationCodec::buildLoginWithPassword(fields, m_request.password,
                                                                      m_request.passwordType, salt, m_codec);
    } catch (const UvException &exception) {
        fail(exception);
        return;
    }

    setState(SessionState::Authenticating);
    armPhaseTimeout(m_request.timeoutMs);
    publishStatistics(true);
    m_relay->sendPayload(login, 1); // UTF-16LE payload -> relay flag 1
}

void ConnectionManager::onRelayPayload(const QByteArray &payload)
{
    if (m_terminating || m_relay == nullptr)
        return;

    QList<QByteArray> fields;
    try {
        fields = protocol::decodeByteArrayFields(payload);
    } catch (const UvException &exception) {
        fail(exception);
        return;
    }
    // `FormReceiveResult` raises for a packet without a discriminator byte; the
    // native client treats it as a protocol violation of the same severity.
    if (fields.isEmpty() || fields.first().isEmpty()) {
        fail(ErrorInfo{ErrorCode::Protocol, QStringLiteral("session packet has no discriminator byte")});
        return;
    }
    try {
        handleSessionPacket(fields);
    } catch (const UvException &exception) {
        fail(exception);
    }
}

void ConnectionManager::handleSessionPacket(const QList<QByteArray> &fields)
{
    const int kind = static_cast<quint8>(fields.first().at(0));

    if (kind == 0) {
        const QSize size = decoder::ScreenDecoder::desktopSizeFromLogin(fields);
        m_decoder.resetCanvas(size);
        m_desktopSize = size;
        m_loginAcknowledged = true;
        sendRelayPayload(protocol::InputCodec::loginAcknowledged(), 0);
        setState(SessionState::Connected);
        disarmPhaseTimeout(); // screen updates are event driven; no idle deadline
        publishStatistics(true);
        emit connected(size);
        // The canvas starts black and its whole area is dirty; publishing that
        // first frame gives the renderer a full-size patch to build its mirror.
        if (decoder::FrameUpdatePtr initial = m_decoder.frameBuffer().takeUpdate(0)) {
            ++m_framesPublished;
            ++m_inFlight;
            emit frameReady(initial);
        }
        publishStatistics(false);
        return;
    }

    if (kind == 1) {
        if (m_state != SessionState::Connected) {
            fail(ErrorInfo{ErrorCode::Protocol,
                           QStringLiteral("a screen update arrived before the login acknowledgement")});
            return;
        }
        handleScreenUpdate(fields);
        return;
    }

    if (kind == 3) {
        if (fields.size() > 1 && fields.at(1).size() == 4) {
            const char *bytes = fields.at(1).constData();
            const qint32 value = static_cast<qint32>(static_cast<quint32>(static_cast<quint8>(bytes[0]))
                | (static_cast<quint32>(static_cast<quint8>(bytes[1])) << 8)
                | (static_cast<quint32>(static_cast<quint8>(bytes[2])) << 16)
                | (static_cast<quint32>(static_cast<quint8>(bytes[3])) << 24));
            ++m_sizeFollowUps;
            sendRelayPayload(protocol::InputCodec::sizeFollowUp(value), 0);
            publishStatistics(false);
            return;
        }
        note(ErrorCode::UnsupportedOperation,
             QStringLiteral("kind 3 packet without a 4-byte field was not answered"),
             QStringLiteral("%1 fields").arg(fields.size()));
        return;
    }

    if (kind == 42 || kind == 43 || kind == 50 || kind == 51) {
        const char *name = kind == 42   ? "Play16BitAudio"
            : kind == 43                ? "PlayEncodedAudio"
            : kind == 50                ? "Play16BitMicrophoneAudio"
                                        : "PlayMicrophoneEncodedAudio";
        note(ErrorCode::UnsupportedOperation,
             QStringLiteral("the partner sent audio (%1) which this client does not play").arg(QString::fromLatin1(name)),
             QStringLiteral("kind %1: the packet was refused, the session continues").arg(kind));
        return;
    }

    note(ErrorCode::UnsupportedOperation,
         QStringLiteral("unknown session packet kind %1 was refused").arg(kind),
         QStringLiteral("%1 fields").arg(fields.size()));
}

void ConnectionManager::handleScreenUpdate(const QList<QByteArray> &fields)
{
    const decoder::ScreenDecoder::Result result = m_decoder.apply(fields);
    // The canvas keeps accumulating dirty rectangles, so a renderer that is behind
    // gets one merged patch instead of a queue of stale ones.
    if (m_inFlight < kMaxInFlightPatches) {
        if (decoder::FrameUpdatePtr update = m_decoder.frameBuffer().takeUpdate(result.applied)) {
            if (!update->isEmpty()) {
                ++m_framesPublished;
                ++m_inFlight;
                emit frameReady(update);
            }
        }
    }
    // The reference acknowledges once the renderer accepted the frame; the native
    // renderer accepts every patch it is handed (it owns a copy), so the
    // acknowledgement is sent as soon as the update has been decoded and queued.
    sendRelayPayload(protocol::InputCodec::screenAcknowledged(), 0);
    publishStatistics(false);
}

void ConnectionManager::releaseFrames(int count)
{
    if (count <= 0)
        return;
    m_inFlight = std::max(0, m_inFlight - count);
    // Publish coalesced changes even if the partner stops sending updates after
    // the in-flight window filled. No frame may be stranded awaiting new input.
    if (m_state == SessionState::Connected && m_inFlight < kMaxInFlightPatches) {
        if (auto pending = m_decoder.frameBuffer().takeUpdate()) {
            ++m_framesPublished;
            ++m_inFlight;
            emit frameReady(pending);
            publishStatistics(false);
        }
    }
}

void ConnectionManager::sendRelayPayload(const QByteArray &payload, quint8 flag)
{
    if (m_relay == nullptr || !m_relay->isReady()) {
        note(ErrorCode::Network, QStringLiteral("cannot send on the relay channel: it is not connected"));
        return;
    }
    m_relay->sendPayload(payload, flag);
}

void ConnectionManager::sendInput(const QByteArray &payload)
{
    if (m_terminating || m_state != SessionState::Connected) {
        note(ErrorCode::Configuration, QStringLiteral("input ignored: the session is not connected"));
        return;
    }
    sendRelayPayload(payload, 0);
}

void ConnectionManager::onRelayFailed(const ErrorInfo &error)
{
    if (m_terminating)
        return;
    fail(error);
}

void ConnectionManager::onRelayClosed()
{
    if (m_terminating)
        return;
    if (m_state == SessionState::Disconnected || m_state == SessionState::Failed)
        return;
    fail(ErrorInfo{ErrorCode::Network, QStringLiteral("the partner closed the relay channel")});
}

void ConnectionManager::publishStatistics(bool force)
{
    const qint64 now = m_statisticsClock.isValid() ? m_statisticsClock.elapsed() : 0;
    if (!force && m_statisticsClock.isValid() && m_lastStatisticsMs >= 0
        && now - m_lastStatisticsMs < kStatisticsIntervalMs) {
        // Rate limited: make sure the snapshot is still published once the
        // interval has passed, so no counter is lost.
        if (m_statisticsTimer != nullptr && !m_statisticsTimer->isActive()) {
            m_statisticsTimer->start(
                static_cast<int>(kStatisticsIntervalMs - (now - m_lastStatisticsMs)));
        }
        return;
    }
    if (m_statisticsTimer != nullptr)
        m_statisticsTimer->stop();
    m_lastStatisticsMs = now;

    SessionStatistics statisticsSnapshot;
    if (m_registration != nullptr) {
        statisticsSnapshot.registrationBytesSent = m_registration->bytesSent();
        statisticsSnapshot.registrationBytesReceived = m_registration->bytesReceived();
        statisticsSnapshot.registrationPeer = m_registration->peerDescription();
        statisticsSnapshot.certificateSummary = m_registration->peerCertificateSummary();
    } else {
        statisticsSnapshot.registrationBytesSent = m_statisticsSnapshot.registrationBytesSent;
        statisticsSnapshot.registrationBytesReceived = m_statisticsSnapshot.registrationBytesReceived;
        statisticsSnapshot.registrationPeer = m_statisticsSnapshot.registrationPeer;
        statisticsSnapshot.certificateSummary = m_statisticsSnapshot.certificateSummary;
    }
    if (m_relay != nullptr) {
        statisticsSnapshot.relayBytesSent = m_relay->bytesSent();
        statisticsSnapshot.relayBytesReceived = m_relay->bytesReceived();
        statisticsSnapshot.relayPeer = m_relay->peerDescription();
        statisticsSnapshot.relayProtocol = m_relay->negotiatedProtocol();
    } else {
        statisticsSnapshot.relayBytesSent = m_statisticsSnapshot.relayBytesSent;
        statisticsSnapshot.relayBytesReceived = m_statisticsSnapshot.relayBytesReceived;
        statisticsSnapshot.relayPeer = m_statisticsSnapshot.relayPeer;
        statisticsSnapshot.relayProtocol = m_statisticsSnapshot.relayProtocol;
    }

    const decoder::ScreenDecoder::Counters counters = m_decoder.counters();
    statisticsSnapshot.updatesDecoded = counters.updates;
    statisticsSnapshot.scrollCopies = counters.scrollCopies;
    statisticsSnapshot.rgb555Tiles = counters.rgb555Tiles;
    statisticsSnapshot.atlasTiles = counters.atlasTiles;
    statisticsSnapshot.cacheReferences = counters.cacheReferences;
    statisticsSnapshot.cacheAdditions = counters.cacheAdditions;
    statisticsSnapshot.framesPublished = m_framesPublished;
    statisticsSnapshot.unsupportedNotices = m_unsupportedNotices;
    statisticsSnapshot.ignoredRegistrationPayloads = m_ignoredRegistrationPayloads;
    statisticsSnapshot.sizeFollowUps = m_sizeFollowUps;
    statisticsSnapshot.clientId = m_clientId;
    statisticsSnapshot.desktopSize = m_desktopSize;
    statisticsSnapshot.dictionaryInstalled = m_decoder.hasDictionary();
    statisticsSnapshot.loginAcknowledged = m_loginAcknowledged;
    statisticsSnapshot.tlsWarning = m_request.tlsPolicy.warningText();

    m_statisticsSnapshot = statisticsSnapshot;
    emit statisticsChanged(statisticsSnapshot);
}

} // namespace ultraview::network
