#pragma once
//
// The QML-facing controller of the application.
//
// It is the only object the QML layer talks to: it owns the session facade, the
// settings, the recent-partner list and the input controller, assembles the
// `network::SessionRequest` from user data and translates worker signals into
// plain properties the pages can bind to.
//
// Secret policy: the partner password lives in this object's memory (or in the
// platform keyring when the user opted in) and is never written into the UI's
// diagnostics text.
//
#include <QByteArray>
#include <QObject>
#include <QSize>
#include <QString>
#include <QVariantList>

#include "core/SessionManager.h"
#include "input/InputController.h"
#include "models/RecentConnectionsModel.h"
#include "models/SettingsModel.h"
#include "render/RemoteScreenItem.h"
#include "security/CredentialStore.h"

namespace ultraview::models {

class ConnectionViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString partnerId READ partnerId WRITE setPartnerId NOTIFY partnerIdChanged)
    Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY passwordChanged)
    Q_PROPERTY(bool hasPassword READ hasPassword NOTIFY passwordChanged)
    Q_PROPERTY(QString stateName READ stateName NOTIFY stateChanged)
    Q_PROPERTY(int stateCode READ stateCode NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY stateChanged)
    Q_PROPERTY(bool canConnect READ canConnect NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(QString noticeText READ noticeText NOTIFY noticeTextChanged)
    Q_PROPERTY(QString desktopSizeText READ desktopSizeText NOTIFY statisticsChanged)
    Q_PROPERTY(QString routeText READ routeText NOTIFY statisticsChanged)
    Q_PROPERTY(QString configurationHint READ configurationHint NOTIFY stateChanged)
    Q_PROPERTY(QString tlsWarning READ tlsWarning NOTIFY settingsChanged)
    Q_PROPERTY(bool inputEnabled READ inputEnabled WRITE setInputEnabled NOTIFY inputEnabledChanged)
    Q_PROPERTY(ultraview::input::InputController *input READ input CONSTANT)
    Q_PROPERTY(ultraview::models::SettingsModel *settings READ settingsObject CONSTANT)
    Q_PROPERTY(ultraview::models::RecentConnectionsModel *recent READ recentObject CONSTANT)
    Q_PROPERTY(ultraview::render::RemoteScreenItem *screen READ screenObject NOTIFY screenChanged)
    Q_PROPERTY(bool dictionaryLoaded READ dictionaryLoaded NOTIFY statisticsChanged)

public:
    explicit ConnectionViewModel(QObject *parent = nullptr);
    ~ConnectionViewModel() override;

    QString partnerId() const { return m_partnerId; }
    void setPartnerId(const QString &id);
    QString password() const { return m_password; }
    void setPassword(const QString &password);
    bool hasPassword() const { return !m_password.isEmpty(); }

    QString stateName() const;
    int stateCode() const;
    bool isConnected() const { return m_session->isConnected(); }
    bool isBusy() const;
    bool canConnect() const;
    QString statusText() const;
    QString errorText() const { return m_errorText; }
    QString noticeText() const { return m_noticeText; }
    QString desktopSizeText() const;
    QString routeText() const;
    QString configurationHint() const;
    QString tlsWarning() const;
    bool inputEnabled() const;
    void setInputEnabled(bool enabled);
    input::InputController *input() { return m_input; }
    SettingsModel *settingsObject() { return m_settings; }
    RecentConnectionsModel *recentObject() { return m_recent; }
    render::RemoteScreenItem *screenObject() { return m_screen; }
    bool dictionaryLoaded() const { return m_dictionaryLoaded; }

    SettingsModel *settings() { return m_settings; }
    RecentConnectionsModel *recent() { return m_recent; }
    SessionManager *session() { return m_session; }

public slots:
    /// Validate the current inputs and start the session.
    void connectNow();
    void disconnectNow();
    /// Receive decoded patches; connects the render item to the session.
    void attachScreenItem(ultraview::render::RemoteScreenItem *item);
    /// Write scaffold profile files into the application config directory when
    /// they do not exist yet and remember their paths.
    QString ensureProfileTemplates();
    /// Read and remember the screen dictionary (8bit.dat).
    QString loadDictionary(const QString &path);
    void clearError();
    void clearNotice();
    /// Credential-free diagnostics text for the About dialog.
    QString diagnostics() const;
    /// Converts a QML file dialog URL into a local path (identity for plain paths).
    Q_INVOKABLE QString localFileFromUrl(const QString &url) const;
    /// {label, value} rows for the statistics panel.
    QVariantList statisticsRows() const;
    /// Re-read the settings (the settings dialog calls this after editing).
    void settingsApplied();

signals:
    void partnerIdChanged();
    void passwordChanged();
    void stateChanged();
    void statusTextChanged();
    void errorTextChanged();
    void noticeTextChanged();
    void statisticsChanged();
    void settingsChanged();
    void inputEnabledChanged();
    void screenChanged();
    /// Raised when a request cannot even be assembled (missing profile, ...).
    void errorRaised(const QString &message);

private:
    void onStateChanged(SessionState state);
    void onConnected(const QSize &size);
    void onFailed(const ErrorInfo &error);
    void onNotice(const ErrorInfo &error);
    void onStatistics(const network::SessionStatistics &statistics);
    void onInputCommand(const QByteArray &payload);
    void onInputStatus(const QString &message);
    void applyViewSettings();
    bool buildSessionRequest(network::SessionRequest *request, QString *error) const;

    SessionManager *m_session = nullptr;
    SettingsModel *m_settings = nullptr;
    RecentConnectionsModel *m_recent = nullptr;
    input::InputController *m_input = nullptr;
    security::CredentialStore *m_credentials = nullptr;
    render::RemoteScreenItem *m_screen = nullptr;

    QString m_partnerId;
    QString m_password;
    QString m_errorText;
    QString m_noticeText;
    QString m_statusText;
    network::SessionStatistics m_statistics;
    bool m_dictionaryLoaded = false;
    bool m_inputEnabled = true;
};

} // namespace ultraview::models
