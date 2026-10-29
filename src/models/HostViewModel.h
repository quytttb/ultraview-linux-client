#pragma once
//
// QML-facing host-mode facade.
//
// `HostViewModel` keeps the `host::HostManager` on a worker thread, loads the
// private host identity lazily (only when the user opens the host page or starts
// hosting) and mirrors the worker state into plain properties.  Hosting itself
// never starts implicitly: `startHosting()` is the only entry point.
//
// Secret policy: the 8-digit password is shown on the local host page because
// the Windows viewer needs it, but it never appears in status text, diagnostics
// or the application log.
//
#include <QObject>
#include <QString>
#include <QThread>

#include "host/HostManager.h"
#include "host/HostProfile.h"

class QTimer;

namespace ultraview::models {

class SettingsModel;

class HostViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString hostId READ hostId NOTIFY identityChanged)
    Q_PROPERTY(QString publicAddress READ publicAddress NOTIFY identityChanged)
    Q_PROPERTY(QString password READ password NOTIFY identityChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY identityChanged)
    Q_PROPERTY(QString localAddress READ localAddress NOTIFY identityChanged)
    Q_PROPERTY(QString profilePath READ profilePath NOTIFY identityChanged)
    Q_PROPERTY(bool profileReady READ profileReady NOTIFY profileChanged)
    Q_PROPERTY(bool profileLoading READ profileLoading NOTIFY profileChanged)
    Q_PROPERTY(QString profileError READ profileError NOTIFY profileChanged)

    Q_PROPERTY(int stateCode READ stateCode NOTIFY stateChanged)
    Q_PROPERTY(QString stateName READ stateName NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool hosting READ isHosting NOTIFY stateChanged)
    Q_PROPERTY(bool starting READ isStarting NOTIFY stateChanged)
    Q_PROPERTY(bool captureSupported READ captureSupported CONSTANT)
    Q_PROPERTY(QString captureHint READ captureHint CONSTANT)
    Q_PROPERTY(QString backendName READ backendName NOTIFY backendChanged)
    Q_PROPERTY(QString layoutText READ layoutText NOTIFY backendChanged)
    Q_PROPERTY(QString tlsWarning READ tlsWarning NOTIFY tlsWarningChanged)

    Q_PROPERTY(bool approvalPending READ approvalPending NOTIFY approvalChanged)
    Q_PROPERTY(QString approvalViewerId READ approvalViewerId NOTIFY approvalChanged)
    Q_PROPERTY(QString approvalViewerName READ approvalViewerName NOTIFY approvalChanged)
    Q_PROPERTY(int approvalSecondsRemaining READ approvalSecondsRemaining NOTIFY approvalChanged)

    Q_PROPERTY(bool viewerConnected READ viewerConnected NOTIFY viewerChanged)
    Q_PROPERTY(QString viewerName READ viewerName NOTIFY viewerChanged)
    Q_PROPERTY(QString viewerId READ viewerId NOTIFY viewerChanged)
    Q_PROPERTY(bool controlAllowed READ controlAllowed WRITE setControlAllowed NOTIFY controlChanged)

    Q_PROPERTY(QString errorText READ errorText NOTIFY errorChanged)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY errorChanged)

public:
    explicit HostViewModel(SettingsModel *settings, QObject *parent = nullptr);
    ~HostViewModel() override;

    QString hostId() const { return m_registeredId; }
    QString publicAddress() const { return m_publicAddress; }
    QString password() const { return m_identity.password; }
    QString displayName() const { return m_identity.displayName; }
    QString localAddress() const { return m_identity.localAddress; }
    QString profilePath() const { return m_identity.profilePath; }
    bool profileReady() const { return m_profileReady; }
    bool profileLoading() const { return m_profileLoading; }
    QString profileError() const { return m_profileError; }

    int stateCode() const { return m_stateCode; }
    QString stateName() const;
    QString statusText() const { return m_statusText; }
    bool isHosting() const;
    bool isStarting() const { return m_startPending; }
    bool captureSupported() const;
    QString captureHint() const;
    QString backendName() const { return m_backendName; }
    QString layoutText() const { return m_layoutText; }
    QString tlsWarning() const;

    bool approvalPending() const { return m_approvalPending; }
    QString approvalViewerId() const { return m_approvalViewerId; }
    QString approvalViewerName() const { return m_approvalViewerName; }
    int approvalSecondsRemaining() const { return m_approvalSecondsRemaining; }

    bool viewerConnected() const { return m_viewerConnected; }
    QString viewerName() const { return m_viewerName; }
    QString viewerId() const { return m_viewerId; }
    bool controlAllowed() const { return m_controlAllowed; }
    void setControlAllowed(bool allowed);

    QString errorText() const { return m_errorText; }
    QString errorDetail() const { return m_errorDetail; }

public slots:
    /// Load (or create) the private host identity.  Safe to call repeatedly.
    void prepare();
    /// The only entry point that starts hosting.  Requires an explicit call.
    void startHosting();
    void stopHosting();

    void approveControl();
    void approveViewOnly();
    void refuse();

signals:
    void identityChanged();
    void profileChanged();
    void stateChanged();
    void statusChanged();
    void backendChanged();
    void tlsWarningChanged();
    void approvalChanged();
    void approvalRequested();
    void approvalCleared();
    void viewerChanged();
    void controlChanged();
    void errorChanged();

private:
    bool loadIdentity(QString *error);
    void clearApproval();
    void clearViewer();
    void setError(const QString &message, const QString &detail);
    void clearError();
    void onWorkerState(int code, const QString &name);
    void onWorkerRegistered(const QString &id, const QString &publicAddress);
    void onWorkerBackend(const QString &name, const QString &layout);
    void onWorkerApproval(const QString &viewerId, const QString &viewerName);
    void onWorkerViewerConnected(const QString &viewerId, const QString &viewerName);
    void onWorkerViewerDisconnected(const QString &reason);
    void onWorkerFailed(const QString &message, const QString &detail);
    void onWorkerStopped();

    SettingsModel *m_settings = nullptr;
    QThread *m_thread = nullptr;
    host::HostManager *m_worker = nullptr;
    QTimer *m_approvalTimer = nullptr;

    host::HostIdentity m_identity;
    bool m_profileReady = false;
    bool m_profileLoading = false;
    QString m_profileError;
    QString m_registeredId;
    QString m_publicAddress;

    int m_stateCode = 0;
    QString m_stateName = QStringLiteral("Not hosting");
    QString m_statusText;
    bool m_startPending = false;
    bool m_terminating = false;
    QString m_backendName;
    QString m_layoutText;

    bool m_approvalPending = false;
    QString m_approvalViewerId;
    QString m_approvalViewerName;
    int m_approvalSecondsRemaining = 0;

    bool m_viewerConnected = false;
    QString m_viewerName;
    QString m_viewerId;
    bool m_controlAllowed = false;

    QString m_errorText;
    QString m_errorDetail;
};

} // namespace ultraview::models
