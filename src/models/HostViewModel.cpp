#include "models/HostViewModel.h"

#include <QMetaObject>
#include <QTimer>

#include <utility>

#include "models/SettingsModel.h"

namespace ultraview::models {
namespace {

constexpr int kApprovalTimeoutSeconds = 45;

QString stateNameFor(int code)
{
    return host::hostStateName(static_cast<host::HostState>(code));
}

} // namespace

HostViewModel::HostViewModel(SettingsModel *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("ultraview-host"));
    m_worker = new host::HostManager();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &host::HostManager::stateChanged, this, &HostViewModel::onWorkerState);
    connect(m_worker, &host::HostManager::statusTextChanged, this, [this](const QString &text) {
        m_statusText = text;
        emit statusChanged();
    });
    connect(m_worker, &host::HostManager::registered, this, &HostViewModel::onWorkerRegistered);
    connect(m_worker, &host::HostManager::backendChanged, this, &HostViewModel::onWorkerBackend);
    connect(m_worker, &host::HostManager::approvalRequested, this,
            &HostViewModel::onWorkerApproval);
    connect(m_worker, &host::HostManager::approvalCleared, this, &HostViewModel::clearApproval);
    connect(m_worker, &host::HostManager::viewerConnected, this,
            &HostViewModel::onWorkerViewerConnected);
    connect(m_worker, &host::HostManager::viewerDisconnected, this,
            &HostViewModel::onWorkerViewerDisconnected);
    connect(m_worker, &host::HostManager::controlChanged, this, [this](bool allowed) {
        if (m_controlAllowed == allowed)
            return;
        m_controlAllowed = allowed;
        emit controlChanged();
    });
    connect(m_worker, &host::HostManager::failed, this, &HostViewModel::onWorkerFailed);
    connect(m_worker, &host::HostManager::stopped, this, &HostViewModel::onWorkerStopped);

    m_approvalTimer = new QTimer(this);
    m_approvalTimer->setInterval(1000);
    connect(m_approvalTimer, &QTimer::timeout, this, [this] {
        if (!m_approvalPending) {
            m_approvalTimer->stop();
            return;
        }
        if (m_approvalSecondsRemaining > 0)
            --m_approvalSecondsRemaining;
        if (m_approvalSecondsRemaining <= 0) {
            // HostSession refuses the viewer on its own deadline.  This timer
            // only closes the local prompt; it never approves anything.
            m_approvalTimer->stop();
            clearApproval();
            return;
        }
        emit approvalChanged();
    });

    if (m_settings != nullptr) {
        connect(m_settings, &SettingsModel::tlsModeChanged, this,
                &HostViewModel::tlsWarningChanged);
    }

    m_stateName = host::hostStateName(host::HostState::Stopped);
    m_statusText = tr("Not hosting. Choose Start hosting to share this computer.");
    m_thread->start();
}

HostViewModel::~HostViewModel()
{
    m_terminating = true;
    if (m_approvalTimer != nullptr)
        m_approvalTimer->stop();
    if (m_thread != nullptr && m_thread->isRunning() && m_worker != nullptr) {
        // Ask the worker to stop, but never use a blocking connection: the
        // desktop portal runs a nested event loop while its permission dialog is
        // open, and a blocking call would freeze application shutdown until that
        // dialog is answered.
        QMetaObject::invokeMethod(m_worker, &host::HostManager::stop,
                                  Qt::QueuedConnection);
        m_thread->quit();
        if (!m_thread->wait(5000)) {
            // The portal is still waiting for the user.  The process is exiting;
            // detach the thread so destroying this object cannot destroy a
            // running QThread.  The worker is deleted by QThread::finished.
            m_thread->setParent(nullptr);
            QObject::connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
        }
    }
    m_identity = {};
}

QString HostViewModel::stateName() const
{
    if (m_startPending && m_stateCode == static_cast<int>(host::HostState::Stopped))
        return tr("Starting host mode");
    return m_stateName;
}

bool HostViewModel::isHosting() const
{
    if (m_startPending)
        return true;
    // Failed and Stopped are terminal; every other state is an active run.
    return m_stateCode != static_cast<int>(host::HostState::Stopped)
        && m_stateCode != static_cast<int>(host::HostState::Failed);
}

bool HostViewModel::captureSupported() const
{
#if defined(ULTRAVIEW_HAVE_WAYLAND_HOST)
    return true;
#else
    return false;
#endif
}

QString HostViewModel::captureHint() const
{
#if defined(ULTRAVIEW_HAVE_WAYLAND_HOST)
    return tr("Screen capture uses the desktop portal (ScreenCast and RemoteDesktop) "
                          "over PipeWire. A Wayland session with xdg-desktop-portal and a portal "
                          "backend is required.");
#else
    return tr("This build has no screen-sharing backend. Rebuild with QtDBus and "
                          "libpipewire-0.3 to enable Wayland portal capture.");
#endif
}

QString HostViewModel::tlsWarning() const
{
    return m_settings != nullptr ? m_settings->tlsWarning() : QString();
}

void HostViewModel::prepare()
{
    if (m_profileReady || m_profileLoading || m_terminating)
        return;
    QString error;
    if (!loadIdentity(&error) && !error.isEmpty()) {
        m_profileError = error;
        emit profileChanged();
    }
}

bool HostViewModel::loadIdentity(QString *error)
{
    if (m_profileLoading)
        return m_profileReady;
    m_profileLoading = true;
    emit profileChanged();

    QString loadError;
    const host::HostIdentity identity = host::HostProfile::loadOrCreate(&loadError);
    m_profileLoading = false;

    if (identity.isValid()) {
        m_profileError.clear();
        m_identity = identity;
        m_profileReady = true;
        emit profileChanged();
        emit identityChanged();
        return true;
    }

    m_profileError = loadError.isEmpty()
        ? tr("The host profile could not be loaded.")
        : loadError;
    if (!m_profileReady)
        m_identity = {};
    emit profileChanged();
    if (error != nullptr)
        *error = m_profileError;
    return false;
}

void HostViewModel::startHosting()
{
    if (m_terminating || isHosting())
        return;

    QString profileError;
    if (!loadIdentity(&profileError)) {
        setError(tr("Host mode cannot start without a host identity."),
                 profileError);
        return;
    }
    if (m_worker == nullptr || m_settings == nullptr) {
        setError(tr("Host mode is unavailable in this build."), {});
        return;
    }

    host::HostStartRequest request;
    request.identity = m_identity;
    request.tlsPolicy = m_settings->tlsPolicy();
    request.timeoutMs = m_settings->timeoutMs();
    request.maxFrameBytes = static_cast<quint32>(m_settings->maxFrameBytes());
    request.registrationHost = m_settings->registrationHostOverride();
    const int registrationPort = m_settings->registrationPortOverride();
    request.registrationPort =
        (registrationPort > 0 && registrationPort <= 65535)
        ? static_cast<quint16>(registrationPort)
        : 0;
    request.registrationHttpHost = m_settings->registrationHttpHostOverride();
    request.registrationPeerName = m_settings->registrationPeerNameOverride();
    request.relayHostOverride = m_settings->relayHostOverride();
    const int relayPort = m_settings->relayPortOverride();
    request.relayPortOverride =
        (relayPort > 0 && relayPort <= 65535) ? static_cast<quint16>(relayPort) : 0;
    request.relayHttpHostOverride = m_settings->relayHttpHostOverride();
    request.relayPeerName = m_settings->relayPeerNameOverride();

    m_startPending = true;
    m_terminating = false;
    clearError();
    clearApproval();
    clearViewer();
    m_registeredId.clear();
    m_publicAddress.clear();
    m_backendName.clear();
    m_layoutText.clear();
    m_statusText = tr("Requesting screen-sharing permission from the desktop…");
    emit stateChanged();
    emit identityChanged();
    emit backendChanged();
    emit viewerChanged();
    emit statusChanged();

    QMetaObject::invokeMethod(
        m_worker,
        [worker = m_worker, request = std::move(request)]() mutable {
            worker->start(request);
            // The worker has its own copy; scrub this closure's copy.
            request.clearSecrets();
        },
        Qt::QueuedConnection);
}

void HostViewModel::stopHosting()
{
    if (m_worker == nullptr || (!isHosting() && !m_startPending))
        return;
    m_startPending = false;
    m_statusText = tr("Stopping host mode…");
    emit statusChanged();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->stop(); },
                              Qt::QueuedConnection);
}

void HostViewModel::approveControl()
{
    if (!m_approvalPending)
        return;
    clearApproval();
    QMetaObject::invokeMethod(
        m_worker, [worker = m_worker] { worker->approve(true); }, Qt::QueuedConnection);
}

void HostViewModel::approveViewOnly()
{
    if (!m_approvalPending)
        return;
    clearApproval();
    QMetaObject::invokeMethod(
        m_worker, [worker = m_worker] { worker->approve(false); }, Qt::QueuedConnection);
}

void HostViewModel::refuse()
{
    if (!m_approvalPending)
        return;
    clearApproval();
    QMetaObject::invokeMethod(
        m_worker, [worker = m_worker] { worker->decline(); }, Qt::QueuedConnection);
}

void HostViewModel::setControlAllowed(bool allowed)
{
    if (!m_viewerConnected || m_worker == nullptr)
        return;
    QMetaObject::invokeMethod(
        m_worker, [worker = m_worker, allowed] { worker->setControlAllowed(allowed); },
        Qt::QueuedConnection);
}

void HostViewModel::clearApproval()
{
    if (!m_approvalPending && m_approvalViewerId.isEmpty() && m_approvalViewerName.isEmpty()
        && m_approvalSecondsRemaining == 0) {
        return;
    }
    const bool wasPending = m_approvalPending;
    m_approvalTimer->stop();
    m_approvalPending = false;
    m_approvalViewerId.clear();
    m_approvalViewerName.clear();
    m_approvalSecondsRemaining = 0;
    emit approvalChanged();
    if (wasPending)
        emit approvalCleared();
}

void HostViewModel::clearViewer()
{
    m_viewerConnected = false;
    m_viewerName.clear();
    m_viewerId.clear();
    m_controlAllowed = false;
}

void HostViewModel::setError(const QString &message, const QString &detail)
{
    m_errorText = message;
    m_errorDetail = detail;
    emit errorChanged();
}

void HostViewModel::clearError()
{
    if (m_errorText.isEmpty() && m_errorDetail.isEmpty())
        return;
    m_errorText.clear();
    m_errorDetail.clear();
    emit errorChanged();
}

void HostViewModel::onWorkerState(int code, const QString &name)
{
    m_startPending = false;
    m_stateCode = code;
    m_stateName = name.isEmpty() ? stateNameFor(code) : name;

    if (code != static_cast<int>(host::HostState::WaitingApproval) && m_approvalPending)
        clearApproval();

    const bool terminal = code == static_cast<int>(host::HostState::Stopped)
        || code == static_cast<int>(host::HostState::Failed);
    if (terminal) {
        const bool hadViewer = m_viewerConnected || !m_viewerName.isEmpty()
            || !m_viewerId.isEmpty();
        const bool hadIdentity = !m_registeredId.isEmpty() || !m_publicAddress.isEmpty();
        if (m_viewerConnected)
            clearViewer();
        m_backendName.clear();
        m_layoutText.clear();
        m_registeredId.clear();
        m_publicAddress.clear();
        if (hadViewer)
            emit viewerChanged();
        emit backendChanged();
        if (hadIdentity)
            emit identityChanged();
    }
    emit stateChanged();
}

void HostViewModel::onWorkerRegistered(const QString &id, const QString &publicAddress)
{
    m_registeredId = id;
    m_publicAddress = publicAddress;
    emit identityChanged();
}

void HostViewModel::onWorkerBackend(const QString &name, const QString &layout)
{
    m_backendName = name;
    m_layoutText = layout;
    emit backendChanged();
}

void HostViewModel::onWorkerApproval(const QString &viewerId, const QString &viewerName)
{
    m_approvalPending = true;
    m_approvalViewerId = viewerId;
    m_approvalViewerName = viewerName;
    m_approvalSecondsRemaining = kApprovalTimeoutSeconds;
    m_approvalTimer->start();
    emit approvalChanged();
    emit approvalRequested();
}

void HostViewModel::onWorkerViewerConnected(const QString &viewerId, const QString &viewerName)
{
    clearApproval();
    m_viewerConnected = true;
    m_viewerId = viewerId;
    m_viewerName = viewerName.isEmpty() ? viewerId : viewerName;
    emit viewerChanged();
}

void HostViewModel::onWorkerViewerDisconnected(const QString &reason)
{
    Q_UNUSED(reason)
    if (!m_viewerConnected && m_viewerName.isEmpty() && m_viewerId.isEmpty())
        return;
    clearViewer();
    emit viewerChanged();
    emit controlChanged();
}

void HostViewModel::onWorkerFailed(const QString &message, const QString &detail)
{
    m_startPending = false;
    clearApproval();
    if (m_viewerConnected) {
        clearViewer();
        emit viewerChanged();
        emit controlChanged();
    }
    m_backendName.clear();
    m_layoutText.clear();
    emit backendChanged();
    setError(message, detail);
}

void HostViewModel::onWorkerStopped()
{
    m_startPending = false;
    clearApproval();
    clearViewer();
    m_registeredId.clear();
    m_publicAddress.clear();
    m_backendName.clear();
    m_layoutText.clear();
    m_statusText = tr("Not hosting. Choose Start hosting to share this computer.");
    clearError();
    emit statusChanged();
    emit identityChanged();
    emit viewerChanged();
    emit controlChanged();
    emit backendChanged();
}

} // namespace ultraview::models
