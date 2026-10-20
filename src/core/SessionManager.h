#pragma once
//
// GUI-facing session facade.
//
// `SessionManager` owns the worker thread that runs `network::ConnectionManager`,
// so the GUI thread never performs TLS, zstd or screen work.  It is the only
// object the view models talk to:
//
//   * `start` / `stop` / `sendInput` are posted to the worker,
//   * every worker signal is re-emitted from the GUI thread (queued connection),
//   * the desktop size, the route and the statistics snapshot are cached here so
//     the UI can read them without touching worker state.
//
#include <QObject>
#include <QSize>
#include <QThread>

#include "core/ErrorTypes.h"
#include "core/SessionState.h"
#include "decoder/FrameBuffer.h"
#include "network/ConnectionManager.h"
#include "protocol/RouteParser.h"

namespace ultraview {

class SessionManager : public QObject {
    Q_OBJECT

public:
    explicit SessionManager(QObject *parent = nullptr);
    ~SessionManager() override;

    /// Cached last states; never read worker members.
    SessionState state() const { return m_state; }
    bool isActive() const { return sessionStateIsActive(m_state); }
    bool isConnected() const { return m_state == SessionState::Connected; }
    QSize desktopSize() const { return m_desktopSize; }
    const network::SessionStatistics &statistics() const { return m_statistics; }
    const protocol::TargetRoute &route() const { return m_route; }
    /// True while the worker thread runs (always true between ctor and dtor).
    bool workerRunning() const { return m_thread != nullptr && m_thread->isRunning(); }

    /// Registers the queued-connection argument types.  Called by the app and by
    /// the tests; safe to call repeatedly.
    static void registerMetaTypes();

public slots:
    /// Posts the request to the worker thread.  Every call ends with exactly one
    /// `failed` or `stopped` signal.
    void start(const ultraview::network::SessionRequest &request);
    void stop();
    /// Posts an input command (see protocol/InputCodec).
    void sendInput(const QByteArray &payload);
    /// The renderer consumed `count` patches; the worker may publish again.
    void releaseFrames(int count);

signals:
    void stateChanged(ultraview::SessionState state);
    void failed(const ultraview::ErrorInfo &error);
    void notice(const ultraview::ErrorInfo &notice);
    /// Log-only message; it never replaces the visible status line.
    void diagnostic(const QString &message);
    void connected(const QSize &desktopSize);
    void routeResolved(const ultraview::protocol::TargetRoute &route);
    void frameReady(const ultraview::decoder::FrameUpdatePtr &update);
    void statisticsChanged(const ultraview::network::SessionStatistics &statistics);
    void stopped();

private:
    void onWorkerStateChanged(SessionState state);
    void onWorkerConnected(const QSize &size);
    void onWorkerStatistics(const network::SessionStatistics &statistics);

    QThread *m_thread = nullptr;
    network::ConnectionManager *m_worker = nullptr;
    SessionState m_state = SessionState::Disconnected;
    QSize m_desktopSize;
    network::SessionStatistics m_statistics;
    protocol::TargetRoute m_route;
};

} // namespace ultraview
