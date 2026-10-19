#include "core/SessionManager.h"

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
    connect(m_worker, &network::ConnectionManager::connected, this, &SessionManager::onWorkerConnected);
    connect(m_worker, &network::ConnectionManager::routeResolved, this, &SessionManager::routeResolved);
    connect(m_worker, &network::ConnectionManager::frameReady, this, &SessionManager::frameReady);
    connect(m_worker, &network::ConnectionManager::statisticsChanged, this,
            &SessionManager::onWorkerStatistics);
    connect(m_worker, &network::ConnectionManager::stopped, this, &SessionManager::stopped);

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
