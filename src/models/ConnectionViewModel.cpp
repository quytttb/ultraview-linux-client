#include "models/ConnectionViewModel.h"

#include "core/ErrorTypes.h"
#include "decoder/ZstdDecoder.h"
#include "models/ProfileLoader.h"
#include "protocol/ControlCommands.h"
#include "transfer/FileTransferService.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QStringList>
#include <QSysInfo>
#include <QUrl>
#include <QtGlobal>

#include <utility>

namespace ultraview::models {
namespace {

QString formatBytes(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KiB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
}

QVariantMap row(const QString &label, const QString &value)
{
    QVariantMap map;
    map.insert(QStringLiteral("label"), label);
    map.insert(QStringLiteral("value"), value);
    return map;
}

} // namespace

ConnectionViewModel::ConnectionViewModel(QObject *parent)
    : QObject(parent)
{
    m_settings = new SettingsModel(this);
    m_recent = new RecentConnectionsModel(this);
    m_input = new input::InputController(this);
    m_credentials = new security::CredentialStore(this);
    m_session = new SessionManager(this);
    m_clipboard = new platform::ClipboardBridge(this);
    m_clipboard->setSession(m_session);

    connect(m_session, &SessionManager::stateChanged, this, &ConnectionViewModel::onStateChanged);
    connect(m_session, &SessionManager::connected, this, &ConnectionViewModel::onConnected);
    connect(m_session, &SessionManager::failed, this, &ConnectionViewModel::onFailed);
    connect(m_session, &SessionManager::notice, this, &ConnectionViewModel::onNotice);
    connect(m_session, &SessionManager::diagnostic, this, [this](const QString &message) {
        appendLog(QStringLiteral("info"), message);
    });
    connect(m_session, &SessionManager::statisticsChanged, this, &ConnectionViewModel::onStatistics);
    connect(m_input, &input::InputController::commandReady, this, &ConnectionViewModel::onInputCommand);
    connect(m_input, &input::InputController::statusMessage, this, &ConnectionViewModel::onInputStatus);

    connect(m_clipboard, &platform::ClipboardBridge::statusMessage, this,
            &ConnectionViewModel::onInputStatus);

    // Peer application messages.  The worker owns the protocol state; this object
    // only caches the last values so QML can bind to plain properties.
    connect(m_session, &SessionManager::chatMessage, this,
            [this](const QString &text) { appendChat(text, false); });
    connect(m_session, &SessionManager::typingChanged, this, [this](bool typing) {
        m_partnerTyping = typing;
        emit typingChanged();
    });
    connect(m_session, &SessionManager::viewOnlyChanged, this, [this](bool viewOnly) {
        m_viewOnly = viewOnly;
        emit viewOnlyChanged();
    });
    connect(m_session, &SessionManager::cursorChanged, this,
            [this](const QImage &image, const QPoint &hotspot) {
                m_cursorImage = image;
                m_cursorHotspot = hotspot;
                if (m_screen != nullptr)
                    m_screen->setRemoteCursor(image, hotspot);
            });
    connect(m_session, &SessionManager::cursorPositionChanged, this,
            [this](const QPoint &position) {
                m_cursorPosition = position;
                m_cursorPositionValid = true;
                if (m_screen != nullptr)
                    m_screen->setCursorPosition(position);
            });
    connect(m_session, &SessionManager::transfersChanged, this,
            [this](const QVariantList &rows) {
                m_transfers = rows;
                int active = 0;
                bool pending = false;
                for (const QVariant &value : rows) {
                    const QVariantMap map = value.toMap();
                    const QString state = map.value(QStringLiteral("state")).toString();
                    if (state == QLatin1String("waiting") || state == QLatin1String("transferring"))
                        ++active;
                    if (state == QLatin1String("waiting")
                        && map.value(QStringLiteral("direction")).toString()
                            == QLatin1String("download")) {
                        pending = true;
                    }
                }
                m_transferActiveCount = active;
                m_transferPending = pending;
                emit transfersChanged();
            });
    connect(m_session, &SessionManager::transferError, this, [this](const QString &message) {
        m_noticeText = message;
        appendLog(QStringLiteral("warning"), message);
        emit noticeTextChanged();
    });
    connect(m_session, &SessionManager::audioStateChanged, this,
            [this](bool enabled, bool outputAvailable, const QString &description) {
                m_audioEnabled = enabled;
                m_audioAvailable = outputAvailable;
                m_audioDescription = description;
                emit audioChanged();
            });

    // The log starts with the build identity: a report from an older running
    // binary can then be told apart from the installed package.
    appendLog(QStringLiteral("info"),
              QCoreApplication::translate("Connection", "Ultraview %1 started (Qt %2, %3)")
                  .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion()),
                       QSysInfo::prettyProductName()));

    const auto applyAndNotify = [this] {
        applyViewSettings();
        emit settingsChanged();
        emit stateChanged();
        emit statusTextChanged();
    };
    connect(m_settings, &SettingsModel::viewChanged, this, [this] {
        applyViewSettings();
        emit settingsChanged();
    });
    connect(m_settings, &SettingsModel::featuresChanged, this, [this] {
        applyFeatureSettings();
        emit settingsChanged();
    });
    connect(m_settings, &SettingsModel::tlsModeChanged, this, [applyAndNotify] { applyAndNotify(); });
    connect(m_settings, &SettingsModel::endpointsChanged, this, [applyAndNotify] { applyAndNotify(); });
    connect(m_settings, &SettingsModel::dictionaryPathChanged, this, [this] {
        m_dictionaryLoaded = !m_settings->dictionaryPath().isEmpty();
        emit settingsChanged();
        emit statisticsChanged();
    });
    connect(m_settings, &SettingsModel::registrationProfilePathChanged, this,
            [applyAndNotify] { applyAndNotify(); });
    connect(m_settings, &SettingsModel::partnerTemplatePathChanged, this,
            [applyAndNotify] { applyAndNotify(); });
}

ConnectionViewModel::~ConnectionViewModel() = default;

void ConnectionViewModel::setPartnerId(const QString &id)
{
    const QString trimmed = id.trimmed();
    if (m_partnerId == trimmed)
        return;
    m_partnerId = trimmed;
    emit partnerIdChanged();
    emit statusTextChanged();
    emit stateChanged();
    if (m_settings->rememberPassword()) {
        if (const auto stored = m_credentials->password(m_partnerId); stored.has_value()) {
            m_password = *stored;
            emit passwordChanged();
        }
    }
}

void ConnectionViewModel::setPassword(const QString &password)
{
    if (m_password == password)
        return;
    m_password = password;
    emit passwordChanged();
    emit stateChanged();
}

QString ConnectionViewModel::stateName() const
{
    return sessionStateName(m_session->state());
}

QString ConnectionViewModel::stateDisplayName() const
{
    switch (m_session->state()) {
    case SessionState::Disconnected:
        return tr("Disconnected");
    case SessionState::Connecting:
        return tr("Connecting");
    case SessionState::Registering:
        return tr("Registering");
    case SessionState::RequestingPartner:
        return tr("Requesting partner");
    case SessionState::RelayConnecting:
        return tr("Connecting to relay");
    case SessionState::Authenticating:
        return tr("Authenticating");
    case SessionState::Connected:
        return tr("Connected");
    case SessionState::Disconnecting:
        return tr("Disconnecting");
    case SessionState::Failed:
        return tr("Failed");
    }
    return stateName();
}

int ConnectionViewModel::stateCode() const
{
    return static_cast<int>(m_session->state());
}

bool ConnectionViewModel::isBusy() const
{
    return sessionStateIsActive(m_session->state()) && !m_session->isConnected();
}

bool ConnectionViewModel::canConnect() const
{
    if (m_session->isActive())
        return false;
    if (m_partnerId.isEmpty())
        return false;
    if (m_settings->registrationProfilePath().isEmpty())
        return false;
    return true;
}

QString ConnectionViewModel::statusText() const
{
    if (!m_errorText.isEmpty())
        return m_errorText;
    switch (m_session->state()) {
    case SessionState::Disconnected:
        return tr("Ready");
    case SessionState::Connecting:
        return tr("Connecting to the registration service…");
    case SessionState::Registering:
        return tr("Registering this controller…");
    case SessionState::RequestingPartner:
        return tr("Requesting partner %1…").arg(m_partnerId);
    case SessionState::RelayConnecting:
        return tr("Connecting to the partner relay…");
    case SessionState::Authenticating:
        return tr("Signing in at the partner…");
    case SessionState::Connected:
        return tr("Connected to %1 (%2)").arg(m_partnerId, desktopSizeText());
    case SessionState::Disconnecting:
        return tr("Disconnecting…");
    case SessionState::Failed:
        return tr("The session failed");
    }
    return QString();
}

QString ConnectionViewModel::desktopSizeText() const
{
    const QSize size = m_statistics.desktopSize;
    if (!size.isValid() || size.isEmpty())
        return tr("unknown size");
    return QStringLiteral("%1 × %2").arg(size.width()).arg(size.height());
}

QString ConnectionViewModel::routeText() const
{
    const protocol::TargetRoute route = m_session->route();
    if (route.tcpServerIp.isEmpty())
        return tr("no route yet");
    return QStringLiteral("%1:%2 via %3").arg(route.publicIp).arg(route.tcpPort).arg(route.tcpServerIp);
}

QString ConnectionViewModel::configurationHint() const
{
    if (m_settings->registrationProfilePath().isEmpty())
        return tr("Select a registration profile (26 terms) in the settings first.");
    if (m_partnerId.isEmpty())
        return tr("Enter the partner ID to control.");
    if (m_password.isEmpty())
        return tr("Enter the partner password.");
    if (!m_dictionaryLoaded && !m_settings->dictionaryPath().isEmpty())
        return tr("The configured screen dictionary could not be read.");
    if (!m_dictionaryLoaded)
        return tr("Without the 8bit.dat screen dictionary, compressed screen updates "
                              "cannot be decoded. Set it in the settings.");
    return QString();
}

QString ConnectionViewModel::tlsWarning() const
{
    return m_settings->tlsWarning();
}

bool ConnectionViewModel::inputEnabled() const
{
    return m_inputEnabled;
}

void ConnectionViewModel::setInputEnabled(bool enabled)
{
    if (m_inputEnabled == enabled)
        return;
    m_inputEnabled = enabled;
    m_input->setEnabled(enabled && m_session->isConnected());
    emit inputEnabledChanged();
}

void ConnectionViewModel::settingsApplied()
{
    m_settings->save();
    applyViewSettings();
    emit settingsChanged();
    emit statusTextChanged();
    emit stateChanged();
}

void ConnectionViewModel::applyViewSettings()
{
    if (m_screen == nullptr)
        return;
    m_screen->setFillMode(static_cast<render::RemoteScreenItem::FillMode>(m_settings->fillMode()));
    m_screen->setSmooth(m_settings->smoothScaling());
}

void ConnectionViewModel::applyFeatureSettings()
{
    m_clipboard->setEnabled(m_settings->clipboardSync());
    if (m_screen != nullptr)
        m_screen->setRemoteCursorVisible(m_settings->remoteCursor());

    // These preferences may be changed from the session-tools popup while a
    // session runs, not only from the settings dialog, so the worker is updated
    // whenever one of them changes.  The cached values keep an unrelated change
    // (say the clipboard toggle) from re-sending a cursor command, and the first
    // call of a session always sends both.
    const bool sendAll = !m_featureSettingsApplied;
    if (sendAll || m_appliedTransferAutoAccept != m_settings->transferAutoAccept()) {
        m_appliedTransferAutoAccept = m_settings->transferAutoAccept();
        m_session->setTransferAutoAccept(m_appliedTransferAutoAccept);
    }
    if (sendAll || m_appliedCursorTracking != m_settings->remoteCursor()) {
        m_appliedCursorTracking = m_settings->remoteCursor();
        m_session->setCursorTracking(m_appliedCursorTracking);
    }
    m_featureSettingsApplied = true;
}

QString ConnectionViewModel::ensureProfileTemplates()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/profiles");
    QDir().mkpath(directory);
    const QString partnerPath = directory + QStringLiteral("/partner-template.json");
    const QString registrationPath = directory + QStringLiteral("/registration-template.json");

    QString error;
    QStringList created;
    if (!QFileInfo::exists(partnerPath)) {
        if (!ProfileLoader::writePartnerTemplate(partnerPath, m_partnerId, false, &error))
            return tr("Could not create the partner template: %1").arg(error);
        created.append(QStringLiteral("partner-template.json"));
    }
    if (!QFileInfo::exists(registrationPath)) {
        if (!ProfileLoader::writeRegistrationTemplate(registrationPath, QStringLiteral("cp1252"), false,
                                                      &error)) {
            return tr("Could not create the registration template: %1").arg(error);
        }
        created.append(QStringLiteral("registration-template.json"));
    }
    m_settings->setPartnerTemplatePath(partnerPath);
    m_settings->setRegistrationProfilePath(registrationPath);
    m_settings->save();
    if (created.isEmpty()) {
        return tr("Templates already present in %1").arg(directory);
    }
    m_noticeText = tr("Created %1 in %2. The registration template still needs the "
                                  "values of this controller's installation.")
                       .arg(created.join(QStringLiteral(", ")), directory);
    emit noticeTextChanged();
    return m_noticeText;
}

QString ConnectionViewModel::loadDictionary(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return tr("Cannot read %1: %2").arg(path, file.errorString());
    const QByteArray data = file.readAll();
    if (data.isEmpty())
        return tr("%1 is empty").arg(path);
    try {
        // Validate with a throw-away decoder so a corrupt file is rejected here
        // instead of during the first compressed screen update.
        decoder::ZstdDecoder probe;
        probe.setDictionary(data);
    } catch (const UvException &exception) {
        return tr("The file is not a usable zstd dictionary: %1").arg(exception.message());
    }
    m_settings->setDictionaryPath(path);
    m_settings->save();
    m_dictionaryLoaded = true;
    emit statisticsChanged();
    return QString();
}

void ConnectionViewModel::clearError()
{
    if (m_errorText.isEmpty())
        return;
    m_errorText.clear();
    emit errorTextChanged();
    emit statusTextChanged();
    emit stateChanged();
}

void ConnectionViewModel::clearNotice()
{
    if (m_noticeText.isEmpty())
        return;
    m_noticeText.clear();
    emit noticeTextChanged();
}

QVariantList ConnectionViewModel::statisticsRows() const
{
    QVariantList rows;
    rows.append(row(tr("State"), stateDisplayName()));
    rows.append(row(tr("Partner"), m_partnerId.isEmpty() ? QStringLiteral("—") : m_partnerId));
    rows.append(row(tr("Controller id"),
                    m_statistics.clientId != 0 ? QString::number(m_statistics.clientId) : QStringLiteral("—")));
    rows.append(row(tr("Desktop"), desktopSizeText()));
    rows.append(row(tr("Route"), routeText()));
    rows.append(row(tr("Registration endpoint"),
                    m_statistics.registrationPeer.isEmpty() ? QStringLiteral("—")
                                                            : m_statistics.registrationPeer));
    rows.append(row(tr("Relay endpoint"),
                    m_statistics.relayPeer.isEmpty() ? QStringLiteral("—") : m_statistics.relayPeer));
    rows.append(row(tr("Tunnel"),
                    m_statistics.relayProtocol.isEmpty() ? QStringLiteral("—") : m_statistics.relayProtocol));
    rows.append(row(QStringLiteral("TLS"), m_settings->tlsModeName()));
    rows.append(row(tr("Sent (registration)"), formatBytes(m_statistics.registrationBytesSent)));
    rows.append(row(tr("Received (registration)"),
                    formatBytes(m_statistics.registrationBytesReceived)));
    rows.append(row(tr("Sent (relay)"), formatBytes(m_statistics.relayBytesSent)));
    rows.append(row(tr("Received (relay)"), formatBytes(m_statistics.relayBytesReceived)));
    rows.append(row(tr("Screen updates decoded"), QString::number(m_statistics.updatesDecoded)));
    rows.append(row(tr("Frames shown"), QString::number(m_statistics.framesPublished)));
    rows.append(row(tr("Compressed rectangles"), QString::number(m_statistics.rgb555Tiles)));
    rows.append(row(tr("JPEG atlas rectangles"), QString::number(m_statistics.atlasTiles)));
    rows.append(row(tr("Cache references"), QString::number(m_statistics.cacheReferences)));
    rows.append(row(tr("Cache additions"), QString::number(m_statistics.cacheAdditions)));
    rows.append(row(tr("Scroll copies"), QString::number(m_statistics.scrollCopies)));
    rows.append(row(tr("Size follow-ups"), QString::number(m_statistics.sizeFollowUps)));
    rows.append(row(tr("Ignored registration payloads"),
                    QString::number(m_statistics.ignoredRegistrationPayloads)));
    rows.append(row(tr("Refused/unsupported packets"),
                    QString::number(m_statistics.unsupportedNotices)));
    rows.append(row(tr("Screen dictionary"),
                    m_dictionaryLoaded ? m_settings->dictionaryStatus() : tr("not loaded")));
    if (!m_statistics.tlsWarning.isEmpty())
        rows.append(row(tr("TLS warning"), m_statistics.tlsWarning));
    return rows;
}

QString ConnectionViewModel::diagnostics() const
{
    QStringList lines;
    lines.append(QCoreApplication::translate("Connection", "UltraViewer native Linux client %1").arg(QStringLiteral(ULTRAVIEW_VERSION)));
    lines.append(QCoreApplication::translate("Connection", "Qt %1").arg(QString::fromLatin1(qVersion())));
    lines.append(QCoreApplication::translate("Connection", "Host %1").arg(QSysInfo::machineHostName()));
    lines.append(QCoreApplication::translate("Connection", "Registration profile: %1")
                     .arg(m_settings->registrationProfilePath().isEmpty()
                              ? QCoreApplication::translate("Connection", "not set")
                              : m_settings->registrationProfilePath()));
    lines.append(QCoreApplication::translate("Connection", "Partner template: %1")
                     .arg(m_settings->partnerTemplatePath().isEmpty()
                              ? QCoreApplication::translate("Connection", "not set")
                              : m_settings->partnerTemplatePath()));
    lines.append(QCoreApplication::translate("Connection", "Screen dictionary: %1").arg(m_settings->dictionaryStatus()));
    lines.append(QCoreApplication::translate("Connection", "TLS mode: %1").arg(m_settings->tlsModeName()));
    if (m_settings->endpointOverrideActive())
        lines.append(QCoreApplication::translate("Connection", "Endpoint overrides are active"));
    lines.append(QString());
    for (const QVariant &value : statisticsRows()) {
        const QVariantMap map = value.toMap();
        lines.append(QStringLiteral("%1: %2").arg(map.value(QStringLiteral("label")).toString(),
                                                  map.value(QStringLiteral("value")).toString()));
    }
    lines.append(QString());
    lines.append(QCoreApplication::translate("Connection", "Passwords are never included in this report."));
    return lines.join(QLatin1Char('\n'));
}

QString ConnectionViewModel::localFileFromUrl(const QString &url) const
{
    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.scheme().isEmpty())
        return url;
    if (parsed.isLocalFile())
        return parsed.toLocalFile();
    return url;
}

bool ConnectionViewModel::buildSessionRequest(network::SessionRequest *request, QString *error) const
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr)
            *error = message;
        return false;
    };

    network::SessionRequest result;
    try {
        if (m_settings->registrationProfilePath().isEmpty())
            return fail(tr("No registration profile selected."));
        const RegistrationProfile registration =
            ProfileLoader::loadRegistrationProfile(m_settings->registrationProfilePath());
        result.registrationTerms = registration.terms;
        result.codec = registration.codec;

        if (!m_settings->partnerTemplatePath().isEmpty()) {
            const PartnerProfile partner =
                ProfileLoader::loadPartnerProfile(m_settings->partnerTemplatePath());
            result.requestTerms = partner.requestTerms;
            // The template's password is only a fallback: whatever the user typed
            // in the connect page wins.
            if (m_password.isEmpty() && !partner.password.isEmpty()) {
                result.password = partner.password;
            }
            result.passwordType = partner.passwordType;
        } else {
            result.requestTerms = ProfileLoader::defaultRequestTerms(m_partnerId);
        }
    } catch (const UvException &exception) {
        return fail(QStringLiteral("%1: %2").arg(exception.message(), exception.detail()));
    } catch (const std::exception &exception) {
        return fail(QString::fromUtf8(exception.what()));
    }

    result.partnerId = m_partnerId;
    if (result.password.isEmpty())
        result.password = m_password;
    if (result.password.isEmpty())
        return fail(tr("Enter the partner password."));
    if (result.requestTerms.value(1).isEmpty() && result.partnerId.isEmpty())
        return fail(tr("Enter the partner ID."));

    result.viewportHint = QSize(m_settings->viewportWidth(), m_settings->viewportHeight());
    result.tlsPolicy = m_settings->tlsPolicy();
    result.timeoutMs = m_settings->timeoutMs();
    result.maxFrameBytes = static_cast<quint32>(m_settings->maxFrameBytes());
    result.decoderOptions = m_settings->decoderOptions();
    result.registrationHost = m_settings->registrationHostOverride();
    result.registrationPort = static_cast<quint16>(m_settings->registrationPortOverride());
    result.registrationHttpHost = m_settings->registrationHttpHostOverride();
    result.registrationPeerName = m_settings->registrationPeerNameOverride();
    result.relayHostOverride = m_settings->relayHostOverride();
    result.relayPortOverride = static_cast<quint16>(m_settings->relayPortOverride());
    result.relayHttpHostOverride = m_settings->relayHttpHostOverride();
    result.relayPeerName = m_settings->relayPeerNameOverride();

    QString dictionaryError;
    result.screenDictionary = m_settings->loadDictionary(&dictionaryError);
    if (result.screenDictionary.isEmpty())
        result.screenDictionary.clear(); // the session warns instead of failing

    QString validationError;
    if (!result.validate(&validationError))
        return fail(validationError);

    *request = result;
    return true;
}

void ConnectionViewModel::connectNow()
{
    clearError();
    clearNotice();

    network::SessionRequest request;
    QString error;
    if (!buildSessionRequest(&request, &error)) {
        m_errorText = error;
        appendLog(QStringLiteral("error"), error);
        emit errorTextChanged();
        emit statusTextChanged();
        emit stateChanged();
        emit errorRaised(error);
        return;
    }

    if (m_screen != nullptr)
        m_screen->clear();

    m_recent->record(m_partnerId);
    if (m_settings->rememberPassword()) {
        QString credentialsError;
        if (!m_credentials->storePassword(m_partnerId, m_password, &credentialsError)) {
            m_noticeText = credentialsError;
            appendLog(QStringLiteral("warning"), credentialsError);
            emit noticeTextChanged();
        }
    } else {
        m_credentials->setSessionPassword(m_partnerId, m_password);
    }
    m_input->releaseAll();
    m_settings->save();

    // Feature choices travel to the worker before `start`, so the connect
    // handshake already reflects them (queued calls to one thread are ordered).
    // A new session must receive every preference, even one unchanged since the
    // previous session.
    m_featureSettingsApplied = false;
    applyFeatureSettings();
    m_session->setAudioEnabled(m_settings->audioEnabled());

    const QString policyWarning = tlsWarning();
    appendLog(policyWarning.isEmpty() ? QStringLiteral("info") : QStringLiteral("warning"),
              policyWarning.isEmpty()
                  ? QCoreApplication::translate("Connection", "TLS: chain and host name verification enabled")
                  : policyWarning);

    const bool hasDictionary = !request.screenDictionary.isEmpty();
    if (!hasDictionary) {
        m_noticeText = tr("No screen dictionary configured: compressed screen updates "
                                      "will be refused. Set 8bit.dat in the settings.");
        appendLog(QStringLiteral("warning"), m_noticeText);
        emit noticeTextChanged();
    }
    m_dictionaryLoaded = hasDictionary;

    m_session->start(request);
}

void ConnectionViewModel::disconnectNow()
{
    m_input->releaseAll();
    m_session->stop();
}

void ConnectionViewModel::attachScreenItem(ultraview::render::RemoteScreenItem *item)
{
    if (m_screen == item)
        return;
    if (m_screen != nullptr) {
        disconnect(m_session, nullptr, m_screen, nullptr);
        disconnect(m_screen, nullptr, m_session, nullptr);
    }
    m_screen = item;
    emit screenChanged();
    if (m_screen == nullptr)
        return;
    connect(m_session, &SessionManager::frameReady, m_screen, &render::RemoteScreenItem::submitUpdate,
            Qt::QueuedConnection);
    connect(m_screen, &render::RemoteScreenItem::framesConsumed, m_session, &SessionManager::releaseFrames,
            Qt::QueuedConnection);
    applyViewSettings();
    // A screen item attached after the first cursor selection must still start
    // with the shape and pointer position the session already reported.
    m_screen->setRemoteCursor(m_cursorImage, m_cursorHotspot);
    if (m_cursorPositionValid)
        m_screen->setCursorPosition(m_cursorPosition);
    m_screen->setRemoteCursorVisible(m_settings->remoteCursor());
}

void ConnectionViewModel::onStateChanged(SessionState state)
{
    const bool connected = state == SessionState::Connected;
    m_input->setEnabled(m_inputEnabled && connected);
    if (!connected && sessionStateIsTerminal(state)) {
        m_password.clear();
        emit passwordChanged();
        m_credentials->clearSessionPasswords();
    }
    appendLog(QStringLiteral("info"), QCoreApplication::translate("Connection", "State: %1").arg(stateName()));
    emit stateChanged();
    emit statusTextChanged();
}

void ConnectionViewModel::onConnected(const QSize &size)
{
    appendLog(QStringLiteral("info"),
              QCoreApplication::translate("Connection", "Connected: partner desktop %1x%2").arg(size.width()).arg(size.height()));
    m_errorText.clear();
    emit errorTextChanged();
    emit statusTextChanged();
    emit statisticsChanged();
}

void ConnectionViewModel::onFailed(const ErrorInfo &error)
{
    m_errorText = error.summary();
    appendLog(QStringLiteral("error"), error.message, error.detail);
    emit errorTextChanged();
    emit statusTextChanged();
}

void ConnectionViewModel::onNotice(const ErrorInfo &error)
{
    m_noticeText = error.summary();
    appendLog(QStringLiteral("warning"), error.message, error.detail);
    emit noticeTextChanged();
}

void ConnectionViewModel::onStatistics(const network::SessionStatistics &statistics)
{
    m_statistics = statistics;
    if (!statistics.dictionaryInstalled && m_dictionaryLoaded && statistics.loginAcknowledged)
        m_dictionaryLoaded = false;
    emit statisticsChanged();
    emit statusTextChanged();
}

void ConnectionViewModel::onInputCommand(const QByteArray &payload)
{
    m_session->sendInput(payload);
}

void ConnectionViewModel::onInputStatus(const QString &message)
{
    m_noticeText = message;
    appendLog(QStringLiteral("info"), message);
    emit noticeTextChanged();
}

void ConnectionViewModel::appendLog(const QString &level, const QString &message,
                                    const QString &detail)
{
    LogEntry entry;
    entry.time = QDateTime::currentDateTime();
    entry.level = level;
    entry.message = message;
    entry.detail = detail;
    m_log.append(entry);
    // A session log is a diagnostic aid, not an audit trail; keep it bounded.
    constexpr int limit = 200;
    while (m_log.size() > limit)
        m_log.removeFirst();
    emit logChanged();
}

QString ConnectionViewModel::logText() const
{
    QStringList lines;
    lines.reserve(m_log.size() * 2);
    for (const LogEntry &entry : m_log) {
        lines.append(QStringLiteral("[%1] %2 %3")
                         .arg(entry.time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                              entry.level.toUpper().leftJustified(7), entry.message));
        if (entry.detail.isEmpty())
            continue;
        const QStringList detailLines = entry.detail.split(QLatin1Char('\n'));
        for (const QString &line : detailLines)
            lines.append(QStringLiteral("    %1").arg(line));
    }
    return lines.join(QLatin1Char('\n'));
}

QString ConnectionViewModel::downloadDirectory() const
{
    return transfer::FileTransferService::defaultIncomingDirectory();
}

void ConnectionViewModel::appendChat(const QString &text, bool outgoing, bool system)
{
    if (text.isEmpty() && !system)
        return;
    QVariantMap entry;
    entry.insert(QStringLiteral("text"), text);
    entry.insert(QStringLiteral("outgoing"), outgoing);
    entry.insert(QStringLiteral("system"), system);
    entry.insert(QStringLiteral("time"),
                 QDateTime::currentDateTime().toString(QStringLiteral("HH:mm")));
    m_chatLog.append(entry);
    constexpr int limit = 300;
    while (m_chatLog.size() > limit)
        m_chatLog.removeFirst();
    if (!outgoing)
        ++m_chatUnread;
    emit chatChanged();
}

bool ConnectionViewModel::sendChatMessage(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;
    if (!m_session->isConnected()) {
        m_noticeText = tr("The chat is only available while connected.");
        emit noticeTextChanged();
        return false;
    }
    if (trimmed.contains(QLatin1String("|||"))
        || trimmed.size() > protocol::ControlCommands::kMaxChatCodeUnits) {
        m_noticeText = tr("The message cannot be sent: it is too long or contains "
                                      "the protocol delimiter.");
        appendLog(QStringLiteral("warning"), m_noticeText);
        emit noticeTextChanged();
        return false;
    }
    m_session->sendChat(trimmed);
    appendChat(trimmed, true);
    return true;
}

void ConnectionViewModel::markChatRead()
{
    if (m_chatUnread == 0)
        return;
    m_chatUnread = 0;
    emit chatChanged();
}

void ConnectionViewModel::clearChat()
{
    if (m_chatLog.isEmpty())
        return;
    m_chatLog.clear();
    m_chatUnread = 0;
    emit chatChanged();
}

void ConnectionViewModel::notifyTyping(bool typing)
{
    if (!m_session->isConnected() || m_localTyping == typing)
        return;
    m_localTyping = typing;
    m_session->sendTyping(typing);
}

void ConnectionViewModel::setAudioEnabled(bool enabled)
{
    m_settings->setAudioEnabled(enabled);
    m_settings->save();
    m_audioEnabled = enabled;
    emit audioChanged();
    m_session->setAudioEnabled(enabled);
}

void ConnectionViewModel::toggleAudio()
{
    setAudioEnabled(!m_audioEnabled);
}

bool ConnectionViewModel::offerFile(const QString &url, QString *error)
{
    const QString path = localFileFromUrl(url);
    const QFileInfo info(path);
    if (path.isEmpty() || !info.exists() || !info.isFile()) {
        if (error != nullptr)
            *error = tr("The file does not exist.");
        m_noticeText = tr("The selected file does not exist.");
        emit noticeTextChanged();
        return false;
    }
    if (!m_session->isConnected()) {
        if (error != nullptr)
            *error = tr("The session is not connected.");
        m_noticeText = tr("A file can only be sent while connected.");
        emit noticeTextChanged();
        return false;
    }
    m_session->offerFile(path);
    return true;
}

void ConnectionViewModel::acceptTransfer(const QString &transferId)
{
    m_session->acceptTransfer(transferId, true);
}

void ConnectionViewModel::declineTransfer(const QString &transferId)
{
    m_session->acceptTransfer(transferId, false);
}

void ConnectionViewModel::cancelTransfer(const QString &transferId)
{
    m_session->cancelTransfer(transferId);
}

void ConnectionViewModel::cancelAllTransfers()
{
    for (const QVariant &value : m_transfers) {
        const QVariantMap map = value.toMap();
        const QString state = map.value(QStringLiteral("state")).toString();
        if (state == QLatin1String("waiting") || state == QLatin1String("transferring"))
            m_session->cancelTransfer(map.value(QStringLiteral("id")).toString());
    }
}

void ConnectionViewModel::requestCtrlAltDel()
{
    m_session->requestCtrlAltDel();
}

void ConnectionViewModel::requestReboot()
{
    m_session->requestReboot();
}

void ConnectionViewModel::requestLock()
{
    m_session->requestLock();
}

void ConnectionViewModel::setWallpaperShown(bool shown)
{
    if (m_wallpaperShown == shown)
        return;
    m_wallpaperShown = shown;
    emit wallpaperShownChanged();
    m_session->setWallpaper(shown);
}

void ConnectionViewModel::setQuality(int value)
{
    if (m_quality == value)
        return;
    if (value != 30 && value != 45 && value != 70)
        return;
    m_quality = value;
    emit qualityChanged();
    m_session->setQuality(value);
}

bool ConnectionViewModel::copyLogToClipboard()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return false;
    clipboard->setText(logText());
    return true;
}

} // namespace ultraview::models
