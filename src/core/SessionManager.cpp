#include "core/SessionManager.h"

#include <QImage>

#include <utility>

namespace ultraview {

SessionManager::SessionManager(QObject *parent)
    : QObject(parent)
{
    registerMetaTypes();

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("ultraview-session"));
    m_worker = new network::ConnectionManager();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &network::ConnectionManager::stateChanged, this,
            &SessionManager::onWorkerStateChanged);
    connect(m_worker, &network::ConnectionManager::failed, this, &SessionManager::failed);
    connect(m_worker, &network::ConnectionManager::notice, this, &SessionManager::notice);
    connect(m_worker, &network::ConnectionManager::diagnostic, this, &SessionManager::diagnostic);
    connect(m_worker, &network::ConnectionManager::connected, this, &SessionManager::onWorkerConnected);
    connect(m_worker, &network::ConnectionManager::routeResolved, this, &SessionManager::routeResolved);
    connect(m_worker, &network::ConnectionManager::frameReady, this, &SessionManager::frameReady);
    connect(m_worker, &network::ConnectionManager::statisticsChanged, this,
            &SessionManager::onWorkerStatistics);
    connect(m_worker, &network::ConnectionManager::stopped, this, &SessionManager::stopped);
    connect(m_worker, &network::ConnectionManager::clipboardText, this, &SessionManager::clipboardText);
    connect(m_worker, &network::ConnectionManager::chatMessage, this, &SessionManager::chatMessage);
    connect(m_worker, &network::ConnectionManager::typingChanged, this, &SessionManager::typingChanged);
    connect(m_worker, &network::ConnectionManager::viewOnlyChanged, this,
            &SessionManager::viewOnlyChanged);
    connect(m_worker, &network::ConnectionManager::cursorChanged, this,
            &SessionManager::cursorChanged);
    connect(m_worker, &network::ConnectionManager::cursorPositionChanged, this,
            &SessionManager::cursorPositionChanged);
    connect(m_worker, &network::ConnectionManager::transfersChanged, this,
            &SessionManager::transfersChanged);
    connect(m_worker, &network::ConnectionManager::transferError, this,
            &SessionManager::transferError);
    connect(m_worker, &network::ConnectionManager::audioStateChanged, this,
            &SessionManager::audioStateChanged);

    connect(this, &SessionManager::routeResolved, this, [this](const protocol::TargetRoute &route) {
        m_route = route;
    });

    m_thread->start();
}

SessionManager::~SessionManager()
{
    if (m_thread == nullptr)
        return;
    if (m_thread->isRunning() && m_worker != nullptr) {
        // Close the sockets from inside the worker thread so no event handler runs
        // after the thread stops.
        QMetaObject::invokeMethod(m_worker, &network::ConnectionManager::stop, Qt::BlockingQueuedConnection);
        m_thread->quit();
        m_thread->wait(5000);
    }
}

void SessionManager::registerMetaTypes()
{
    qRegisterMetaType<SessionState>("ultraview::SessionState");
    qRegisterMetaType<network::SessionRequest>("ultraview::network::SessionRequest");
    qRegisterMetaType<network::SessionStatistics>("ultraview::network::SessionStatistics");
    qRegisterMetaType<protocol::TargetRoute>("ultraview::protocol::TargetRoute");
    qRegisterMetaType<decoder::FrameUpdatePtr>("ultraview::decoder::FrameUpdatePtr");
    qRegisterMetaType<ErrorInfo>("ultraview::ErrorInfo");
    qRegisterMetaType<ErrorCode>("ultraview::ErrorCode");
}

void SessionManager::start(const ultraview::network::SessionRequest &request)
{
    if (!m_thread->isRunning())
        m_thread->start();
    // An explicit copy makes the captured member non-const (a capture of a const
    // reference would keep the const qualification) so the password can be
    // scrubbed once the worker has taken its own copy.
    network::SessionRequest requestCopy = request;
    QMetaObject::invokeMethod(
        m_worker,
        [worker = m_worker, request = std::move(requestCopy)]() mutable {
            worker->start(request);
            // The request copy in this closure holds the password; scrub it as
            // soon as the worker has taken its own copy.
            request.clearSecrets();
        },
        Qt::QueuedConnection);
}

void SessionManager::stop()
{
    if (!m_thread->isRunning())
        return;
    QMetaObject::invokeMethod(m_worker, &network::ConnectionManager::stop, Qt::QueuedConnection);
}

void SessionManager::sendInput(const QByteArray &payload)
{
    if (!m_thread->isRunning())
        return;
    QMetaObject::invokeMethod(
        m_worker, [worker = m_worker, payload] { worker->sendInput(payload); }, Qt::QueuedConnection);
}

void SessionManager::releaseFrames(int count)
{
    if (!m_thread->isRunning() || count <= 0)
        return;
    QMetaObject::invokeMethod(
        m_worker, [worker = m_worker, count] { worker->releaseFrames(count); }, Qt::QueuedConnection);
}

namespace {

/// Post `call` to the worker thread.  Every command is a tiny lambda so the GUI
/// never has to know the worker's API beyond SessionManager.
template <typename Function>
void post(network::ConnectionManager *worker, Function &&call)
{
    if (worker == nullptr)
        return;
    QMetaObject::invokeMethod(worker, std::forward<Function>(call), Qt::QueuedConnection);
}

} // namespace

void SessionManager::sendClipboard(const QString &text)
{
    post(m_worker, [worker = m_worker, text] { worker->sendClipboard(text); });
}

void SessionManager::sendChat(const QString &text)
{
    post(m_worker, [worker = m_worker, text] { worker->sendChat(text); });
}

void SessionManager::sendTyping(bool started)
{
    post(m_worker, [worker = m_worker, started] { worker->sendTyping(started); });
}

void SessionManager::setAudioEnabled(bool enabled)
{
    post(m_worker, [worker = m_worker, enabled] { worker->setAudioEnabled(enabled); });
}

void SessionManager::setCursorTracking(bool enabled)
{
    post(m_worker, [worker = m_worker, enabled] { worker->setCursorTracking(enabled); });
}

void SessionManager::setWallpaper(bool shown)
{
    post(m_worker, [worker = m_worker, shown] { worker->setWallpaper(shown); });
}

void SessionManager::setQuality(int value)
{
    post(m_worker, [worker = m_worker, value] { worker->setQuality(value); });
}

void SessionManager::requestCtrlAltDel()
{
    post(m_worker, [worker = m_worker] { worker->requestCtrlAltDel(); });
}

void SessionManager::requestReboot()
{
    post(m_worker, [worker = m_worker] { worker->requestReboot(); });
}

void SessionManager::requestLock()
{
    post(m_worker, [worker = m_worker] { worker->requestLock(); });
}

void SessionManager::offerFile(const QString &localPath)
{
    post(m_worker, [worker = m_worker, localPath] { worker->offerFile(localPath); });
}

void SessionManager::acceptTransfer(const QString &transferId, bool accept)
{
    post(m_worker,
         [worker = m_worker, transferId, accept] { worker->acceptTransfer(transferId, accept); });
}

void SessionManager::cancelTransfer(const QString &transferId)
{
    post(m_worker, [worker = m_worker, transferId] { worker->cancelTransfer(transferId); });
}

void SessionManager::setTransferAutoAccept(bool enabled)
{
    post(m_worker, [worker = m_worker, enabled] { worker->setTransferAutoAccept(enabled); });
}

void SessionManager::onWorkerStateChanged(SessionState state)
{
    m_state = state;
    if (state == SessionState::Disconnected || state == SessionState::Connecting) {
        m_desktopSize = QSize();
        m_route = protocol::TargetRoute{};
    }
    emit stateChanged(state);
}

void SessionManager::onWorkerConnected(const QSize &size)
{
    m_desktopSize = size;
    emit connected(size);
}

void SessionManager::onWorkerStatistics(const network::SessionStatistics &statistics)
{
    m_statistics = statistics;
    emit statisticsChanged(statistics);
}

} // namespace ultraview
