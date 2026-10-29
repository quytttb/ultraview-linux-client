#include "network/ConnectionManager.h"

#include "protocol/AuthenticationCodec.h"
#include "protocol/ClipboardCodec.h"
#include "protocol/ControlCommands.h"
#include "protocol/FileTransferCodec.h"
#include "protocol/InputCodec.h"
#include "protocol/RegistrationCodec.h"
#include "protocol/ZstdCodec.h"

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

    // Remote computer sound.  Decoding stays in this thread; the sink buffers, so
    // a stalled audio device never blocks the session.
    m_audio = new audio::AudioPlayer(this);
    connect(m_audio, &audio::AudioPlayer::streamRequested, this, [this](bool enabled) {
        if (m_terminating || m_state != SessionState::Connected)
            return;
        if (m_relay == nullptr || !m_relay->isReady())
            return;
        const protocol::CommandPayload command = protocol::ControlCommands::audio(enabled);
        sendRelayPayload(command.payload, command.flag);
        emit audioStateChanged(m_audioEnabled, m_audio->hasOutput(), m_audio->outputDescription());
    });
    connect(m_audio, &audio::AudioPlayer::notice, this,
            [this](const QString &message) { inform(message); });

    // File transfer.  The service owns the files; only the relay payloads leave
    // this object.
    m_transfers = new transfer::FileTransferService(this);
    connect(m_transfers, &transfer::FileTransferService::outbound, this,
            [this](const QByteArray &payload) {
                if (!m_terminating && m_state == SessionState::Connected)
                    sendRelayPayload(payload, 0);
            });
    connect(m_transfers, &transfer::FileTransferService::transfersChanged, this,
            &ConnectionManager::transfersChanged);
    connect(m_transfers, &transfer::FileTransferService::notice, this,
            [this](const QString &message) {
                ++m_transferMessages;
                inform(message);
            });
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
    resetSessionFeatures();
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
    m_clipboardUpdates = 0;
    m_chatMessages = 0;
    m_audioPackets = 0;
    m_transferMessages = 0;
    m_cursorShapes = 0;
    m_cursorRequests = 0;
    m_sizeFollowUps = 0;
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

void ConnectionManager::resetSessionFeatures()
{
    m_cursorCache.clear();
    m_cursor = decoder::RemoteCursor{};
    m_cursorPosition = QPoint();
    m_pendingCursorHash = 0;
    m_audio->reset();
    if (m_audio->isEnabled())
        m_audio->setEnabled(false); // the user preference in m_audioEnabled is kept
    m_transfers->cancelAll();
    emit cursorChanged(QImage(), QPoint());
    emit transfersChanged(m_transfers->rows());
    emit audioStateChanged(m_audioEnabled, false, m_audio->outputDescription());
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
        emit diagnostic(QStringLiteral("registration channel lost after the route; the relay "
                                       "session is unaffected (%1)")
                            .arg(error.summary()));
        return;
    }
    fail(error);
}

void ConnectionManager::onRegistrationClosed()
{
    if (m_terminating)
        return;
    if (m_drainingRegistration) {
        emit diagnostic(QStringLiteral("registration channel closed by the service after the route; "
                                       "the relay session is unaffected"));
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

        // The vendor relay presents only its leaf certificate, while the
        // registration endpoint also sends the intermediate.  That chain was just
        // verified against the system store, so reuse its issuers (leaf first, so
        // skip index 0) for the relay instead of weakening verification.
        const QList<QSslCertificate> registrationChain = m_registration->peerCertificateChain();
        if (registrationChain.size() > 1)
            m_request.tlsPolicy.addCaCertificates(registrationChain.mid(1));
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
    connect(m_relay, &RelayTransport::payloadFrom, this,
            [this](const QByteArray &payload, quint32, quint16, bool unicode) {
                onRelayPayload(payload, unicode);
            });
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

void ConnectionManager::onRelayPayload(const QByteArray &payload, bool unicode)
{
    if (m_terminating || m_relay == nullptr)
        return;

    if (unicode) {
        // UTF-16LE text: the legacy clipboard and chat forms arrive this way, as
        // do the bare typing indicators.
        const std::optional<QString> text = protocol::ClipboardCodec::fromUtf16(payload);
        if (!text.has_value()) {
            note(ErrorCode::Protocol,
                 QStringLiteral("a UTF-16 session payload has an odd number of bytes"),
                 QStringLiteral("%1 bytes").arg(payload.size()));
            return;
        }
        handleTextPayload(*text);
        return;
    }

    QList<QByteArray> fields;
    try {
        fields = protocol::decodeByteArrayFields(payload);
    } catch (const UvException &exception) {
        // A delimited text message is not a field array.  The file transfer
        // commands are the only ones the host may send in that shape.
        if (handleRawTextCommand(payload))
            return;
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

void ConnectionManager::handleTextPayload(const QString &text)
{
    const QString clipboardPrefix = QStringLiteral("5") + QStringLiteral("|||");
    const QString chatPrefix = QStringLiteral("12") + QStringLiteral("|||");
    if (text.startsWith(clipboardPrefix)) {
        ++m_clipboardUpdates;
        emit clipboardText(text.mid(clipboardPrefix.size()));
        publishStatistics(false);
        return;
    }
    if (text.startsWith(chatPrefix)) {
        ++m_chatMessages;
        emit chatMessage(text.mid(chatPrefix.size()));
        publishStatistics(false);
        return;
    }
    if (text == QStringLiteral("29") || text == QStringLiteral("30")) {
        emit typingChanged(text == QStringLiteral("29"));
        return;
    }
    inform(QStringLiteral("the partner sent an unrecognized text message (%1 characters)")
               .arg(text.size()));
}

bool ConnectionManager::handleRawTextCommand(const QByteArray &payload)
{
    static const char *const prefixes[] = {"13|||", "14|||", "15|||", "16|||", "21|||"};
    for (const char *prefix : prefixes) {
        if (payload.startsWith(prefix)) {
            ++m_transferMessages;
            m_transfers->handleInboundText(payload);
            return true;
        }
    }
    return false;
}

void ConnectionManager::handleCursorSelection(qint32 hash)
{
    if (hash == decoder::CursorDecoder::kIgnoredSelectionHash) {
        m_cursor = decoder::RemoteCursor{};
        m_pendingCursorHash = 0;
        emit cursorChanged(QImage(), QPoint());
        return;
    }
    if (const std::optional<decoder::RemoteCursor> cached = m_cursorCache.find(hash)) {
        m_cursor = *cached;
        m_pendingCursorHash = 0;
        emit cursorChanged(m_cursor.image, m_cursor.hotspot);
        return;
    }
    // A selection whose shape is unknown is answered with the reference's
    // `|||4<hash>` request, the same bytes the live service answered in 0.2.6.
    // The reference sent it for every selection; the native client only asks for
    // shapes it does not have and remembers the request, so a burst of selections
    // cannot become a request storm.  (`ControlCommands::cursorShapeRequest`
    // builds the documentation's `4|||<hash>` spelling of the same request; the
    // live service has only been observed answering the reference's form.)
    if (m_cursorCache.shouldRequest(hash)) {
        sendConnectedCommand(protocol::InputCodec::sizeFollowUp(hash), 0);
        ++m_cursorRequests;
        // The statistics panel has always counted these as "size follow-ups".
        ++m_sizeFollowUps;
    }
    m_pendingCursorHash = hash;
}

void ConnectionManager::handleCursorShape(const QList<QByteArray> &fields)
{
    if (fields.size() < 4 || fields.at(1).size() != 4 || fields.at(2).size() != 4) {
        note(ErrorCode::UnsupportedOperation,
             QStringLiteral("a cursor shape packet is incomplete"),
             QStringLiteral("%1 fields").arg(fields.size()));
        return;
    }
    const auto readI32 = [](const QByteArray &bytes) {
        return static_cast<qint32>(static_cast<quint32>(static_cast<quint8>(bytes.at(0)))
                                   | (static_cast<quint32>(static_cast<quint8>(bytes.at(1))) << 8)
                                   | (static_cast<quint32>(static_cast<quint8>(bytes.at(2))) << 16)
                                   | (static_cast<quint32>(static_cast<quint8>(bytes.at(3))) << 24));
    };
    const qint32 hash = readI32(fields.at(1));
    const qint32 declared = readI32(fields.at(2));
    if (declared <= 0 || declared > decoder::CursorDecoder::kMaxCurBytes) {
        note(ErrorCode::Resource,
             QStringLiteral("a cursor shape packet declares an unusable length"),
             QStringLiteral("%1 bytes").arg(declared));
        return;
    }
    const QByteArray cur = protocol::ZstdCodec::decompress(
        fields.at(3), declared, decoder::CursorDecoder::kMaxCurBytes);
    const decoder::RemoteCursor cursor = decoder::CursorDecoder::decode(cur, hash);
    m_cursorCache.insert(cursor);
    ++m_cursorShapes;
    if (hash == m_pendingCursorHash || (m_cursor.isValid() && m_cursor.hash == hash)) {
        m_cursor = cursor;
        m_pendingCursorHash = 0;
        emit cursorChanged(m_cursor.image, m_cursor.hotspot);
    }
    publishStatistics(false);
}

void ConnectionManager::handleCursorPosition(const QList<QByteArray> &fields)
{
    const auto readI16 = [](const QByteArray &bytes) {
        return static_cast<qint16>(static_cast<quint16>(static_cast<quint8>(bytes.at(0)))
                                   | (static_cast<quint16>(static_cast<quint8>(bytes.at(1))) << 8));
    };
    if (fields.size() < 3 || fields.at(1).size() != 2 || fields.at(2).size() != 2) {
        note(ErrorCode::UnsupportedOperation,
             QStringLiteral("a pointer position packet is incomplete"),
             QStringLiteral("%1 fields").arg(fields.size()));
        return;
    }
    m_cursorPosition = QPoint(readI16(fields.at(1)), readI16(fields.at(2)));
    emit cursorPositionChanged(m_cursorPosition);
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

        // Feature negotiation.  Each command is only sent when the feature is
        // enabled, so a user who disabled remote audio or the remote cursor never
        // asks the partner to spend bandwidth on it.
        if (m_cursorTracking) {
            const protocol::CommandPayload tracking =
                protocol::ControlCommands::cursorTracking(true);
            sendRelayPayload(tracking.payload, tracking.flag);
        }
        if (m_audioEnabled) {
            // The preference may already be enabled from before the handshake, in
            // which case `setEnabled` is a no-op and emits no request.  Send the
            // documented command 46 toggle exactly once either way: the signal
            // handler already sends it when `setEnabled` does emit.
            const bool alreadyEnabled = m_audio->isEnabled();
            m_audio->setEnabled(true);
            if (alreadyEnabled) {
                const protocol::CommandPayload audio =
                    protocol::ControlCommands::audio(true);
                sendRelayPayload(audio.payload, audio.flag);
            }
        }
        m_transfers->cancelAll(); // nothing survives a reconnect
        emit transfersChanged(m_transfers->rows());
        emit audioStateChanged(m_audioEnabled, m_audio->hasOutput(), m_audio->outputDescription());

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
            handleCursorSelection(value);
            return;
        }
        note(ErrorCode::UnsupportedOperation,
             QStringLiteral("a cursor selection without a 4-byte hash was not answered"),
             QStringLiteral("%1 fields").arg(fields.size()));
        return;
    }

    if (kind == 4) {
        handleCursorShape(fields);
        return;
    }

    if (kind == 5) {
        // Legacy clipboard as a multipart payload: the text is the second field.
        if (fields.size() < 2) {
            note(ErrorCode::UnsupportedOperation,
                 QStringLiteral("a legacy clipboard message carries no text"));
            return;
        }
        const std::optional<QString> text = protocol::ClipboardCodec::fromUtf16(fields.at(1));
        if (!text.has_value()) {
            note(ErrorCode::Protocol,
                 QStringLiteral("a legacy clipboard message is not UTF-16LE"));
            return;
        }
        ++m_clipboardUpdates;
        emit clipboardText(*text);
        publishStatistics(false);
        return;
    }

    if (kind == 12) {
        const std::optional<QString> text = protocol::ControlCommands::parseChat(fields);
        if (!text.has_value()) {
            note(ErrorCode::UnsupportedOperation, QStringLiteral("a chat message could not be decoded"));
            return;
        }
        ++m_chatMessages;
        emit chatMessage(*text);
        publishStatistics(false);
        return;
    }

    if ((kind >= 13 && kind <= 16) || kind == 21) {
        ++m_transferMessages;
        m_transfers->handleInbound(fields);
        return;
    }

    if (kind == 27 || kind == 41) {
        inform(kind == 27
                   ? QStringLiteral("the partner closed the session")
                   : QStringLiteral("the partner terminated the session"));
        stop();
        return;
    }

    if (kind == 28) {
        handleCursorPosition(fields);
        return;
    }

    if (kind == 29 || kind == 30) {
        emit typingChanged(kind == 29);
        return;
    }

    if (kind == 33) {
        const std::optional<QString> text = protocol::ClipboardCodec::parseModern(fields);
        if (!text.has_value()) {
            note(ErrorCode::UnsupportedOperation,
                 QStringLiteral("a clipboard message could not be decoded"));
            return;
        }
        ++m_clipboardUpdates;
        emit clipboardText(*text);
        publishStatistics(false);
        return;
    }

    if (kind == 42) {
        // Audio silence: a byte count in the second field.
        if (fields.size() < 2 || fields.at(1).isEmpty()) {
            note(ErrorCode::UnsupportedOperation,
                 QStringLiteral("an audio silence packet carries no byte count"));
            return;
        }
        quint64 count = 0;
        for (int index = std::min<int>(static_cast<int>(fields.at(1).size()), 4) - 1; index >= 0; --index)
            count = (count << 8) | static_cast<quint8>(fields.at(1).at(index));
        m_audio->submitSilence(static_cast<int>(std::min<quint64>(count, 1u << 20)));
        ++m_audioPackets;
        publishStatistics(false);
        return;
    }

    if (kind == 43) {
        if (fields.size() < 2) {
            note(ErrorCode::UnsupportedOperation,
                 QStringLiteral("an audio packet carries no payload"));
            return;
        }
        m_audio->submitEncoded(fields.at(1));
        ++m_audioPackets;
        publishStatistics(false);
        return;
    }

    if (kind == 48) {
        // The host changed its tile size (8, 16 or 32 pixels).
        if (fields.size() < 2 || fields.at(1).isEmpty()) {
            note(ErrorCode::UnsupportedOperation,
                 QStringLiteral("a tile size packet carries no value"));
            return;
        }
        const int tile = static_cast<quint8>(fields.at(1).at(0));
        if (tile != 8 && tile != 16 && tile != 32) {
            inform(QStringLiteral("the partner announced an unsupported tile size (%1); "
                                  "the current size is kept")
                       .arg(tile));
            return;
        }
        decoder::ScreenDecoder::Options options = m_decoder.options();
        options.blockSize = tile;
        m_decoder.setOptions(options);
        emit diagnostic(QStringLiteral("the partner changed the tile size to %1").arg(tile));
        return;
    }

    if (kind == 49) {
        if (const std::optional<bool> viewOnly = protocol::ControlCommands::parseViewOnly(fields)) {
            emit viewOnlyChanged(*viewOnly);
            if (*viewOnly)
                inform(QStringLiteral("the partner put this session into view-only mode"));
            return;
        }
        note(ErrorCode::UnsupportedOperation,
             QStringLiteral("a view-only packet carries no state"));
        return;
    }

    if (kind == 50 || kind == 51) {
        const char *name = kind == 50 ? "Play16BitMicrophoneAudio"
                                      : "PlayMicrophoneEncodedAudio";
        note(ErrorCode::UnsupportedOperation,
             QStringLiteral("the partner sent microphone audio (%1), which this client does "
                            "not record")
                 .arg(QString::fromLatin1(name)),
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

void ConnectionManager::sendConnectedCommand(const QByteArray &payload, quint8 flag)
{
    if (m_terminating || m_state != SessionState::Connected) {
        inform(QStringLiteral("the command was ignored: the session is not connected"));
        return;
    }
    sendRelayPayload(payload, flag);
}

void ConnectionManager::inform(const QString &message)
{
    emit notice(ErrorInfo{ErrorCode::None, message});
}

void ConnectionManager::sendClipboard(const QString &text)
{
    try {
        const protocol::ClipboardCodec::Upload upload = protocol::ClipboardCodec::buildUpload(text);
        sendConnectedCommand(upload.payload, upload.flag);
    } catch (const UvException &exception) {
        inform(exception.message());
    }
}

void ConnectionManager::sendChat(const QString &text)
{
    try {
        const protocol::CommandPayload command = protocol::ControlCommands::chat(text);
        sendConnectedCommand(command.payload, command.flag);
    } catch (const UvException &exception) {
        inform(exception.message());
    }
}

void ConnectionManager::sendTyping(bool started)
{
    const protocol::CommandPayload command = protocol::ControlCommands::typing(started);
    sendConnectedCommand(command.payload, command.flag);
}

void ConnectionManager::setAudioEnabled(bool enabled)
{
    m_audioEnabled = enabled;
    if (enabled) {
        // Opening the device is lazy: the first audio packet does it.  The command
        // is sent immediately so the host starts capturing right away.
        m_audio->setEnabled(true);
    } else {
        m_audio->setEnabled(false);
    }
    emit audioStateChanged(m_audioEnabled, m_audio->hasOutput(), m_audio->outputDescription());
}

void ConnectionManager::setCursorTracking(bool enabled)
{
    m_cursorTracking = enabled;
    if (!enabled) {
        m_cursor = decoder::RemoteCursor{};
        m_pendingCursorHash = 0;
        emit cursorChanged(QImage(), QPoint());
    }
    // Before the handshake the preference is only recorded; the connect path
    // sends it once the relay carries peer messages.
    if (m_state != SessionState::Connected)
        return;
    const protocol::CommandPayload command = protocol::ControlCommands::cursorTracking(enabled);
    sendConnectedCommand(command.payload, command.flag);
}

void ConnectionManager::setWallpaper(bool shown)
{
    m_wallpaperShown = shown;
    const protocol::CommandPayload command = protocol::ControlCommands::wallpaper(shown);
    sendConnectedCommand(command.payload, command.flag);
}

void ConnectionManager::setQuality(int value)
{
    try {
        const protocol::CommandPayload command = protocol::ControlCommands::quality(value);
        sendConnectedCommand(command.payload, command.flag);
    } catch (const UvException &exception) {
        inform(exception.message());
    }
}

void ConnectionManager::requestCtrlAltDel()
{
    const protocol::CommandPayload command = protocol::ControlCommands::ctrlAltDel();
    sendConnectedCommand(command.payload, command.flag);
}

void ConnectionManager::requestReboot()
{
    const protocol::CommandPayload command = protocol::ControlCommands::reboot();
    sendConnectedCommand(command.payload, command.flag);
}

void ConnectionManager::requestLock()
{
    const protocol::CommandPayload command = protocol::ControlCommands::lockWorkstation();
    sendConnectedCommand(command.payload, command.flag);
}

void ConnectionManager::offerFile(const QString &localPath)
{
    QString error;
    const QString transferId = m_transfers->offerFile(localPath, &error);
    if (transferId.isEmpty())
        emit transferError(error);
}

void ConnectionManager::acceptTransfer(const QString &transferId, bool accept)
{
    m_transfers->acceptIncoming(transferId, accept);
}

void ConnectionManager::cancelTransfer(const QString &transferId)
{
    m_transfers->cancel(transferId);
}

void ConnectionManager::setTransferAutoAccept(bool enabled)
{
    m_transfers->setAutoAccept(enabled);
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
    statisticsSnapshot.clipboardUpdates = m_clipboardUpdates;
    statisticsSnapshot.chatMessages = m_chatMessages;
    statisticsSnapshot.cursorShapes = m_cursorShapes;
    statisticsSnapshot.cursorRequests = m_cursorRequests;
    statisticsSnapshot.audioPackets = m_audioPackets;
    statisticsSnapshot.transferMessages = m_transferMessages;
    statisticsSnapshot.audioEnabled = m_audioEnabled;
    statisticsSnapshot.audioOutput = m_audio != nullptr && m_audio->hasOutput();
    statisticsSnapshot.clientId = m_clientId;
    statisticsSnapshot.desktopSize = m_desktopSize;
    statisticsSnapshot.dictionaryInstalled = m_decoder.hasDictionary();
    statisticsSnapshot.loginAcknowledged = m_loginAcknowledged;
    statisticsSnapshot.tlsWarning = m_request.tlsPolicy.warningText();

    m_statisticsSnapshot = statisticsSnapshot;
    emit statisticsChanged(statisticsSnapshot);
}

} // namespace ultraview::network
