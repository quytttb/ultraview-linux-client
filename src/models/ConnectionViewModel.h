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
#include <QDateTime>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QVariantList>

#include "core/SessionManager.h"
#include "input/InputController.h"
#include "models/RecentConnectionsModel.h"
#include "models/SettingsModel.h"
#include "platform/ClipboardBridge.h"
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
    Q_PROPERTY(QString logText READ logText NOTIFY logChanged)
    Q_PROPERTY(int logCount READ logCount NOTIFY logChanged)
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
    // --- session tools (peer application messages) -------------------------
    Q_PROPERTY(QVariantList chatLog READ chatLog NOTIFY chatChanged)
    Q_PROPERTY(int chatUnread READ chatUnread NOTIFY chatChanged)
    Q_PROPERTY(bool partnerTyping READ partnerTyping NOTIFY typingChanged)
    Q_PROPERTY(QVariantList transfers READ transfers NOTIFY transfersChanged)
    Q_PROPERTY(int transferActiveCount READ transferActiveCount NOTIFY transfersChanged)
    Q_PROPERTY(bool transferPending READ transferPending NOTIFY transfersChanged)
    Q_PROPERTY(QString downloadDirectory READ downloadDirectory CONSTANT)
    Q_PROPERTY(bool audioEnabled READ audioEnabled NOTIFY audioChanged)
    Q_PROPERTY(bool audioAvailable READ audioAvailable NOTIFY audioChanged)
    Q_PROPERTY(QString audioDescription READ audioDescription NOTIFY audioChanged)
    Q_PROPERTY(bool viewOnly READ viewOnly NOTIFY viewOnlyChanged)
    Q_PROPERTY(bool wallpaperShown READ wallpaperShown NOTIFY wallpaperShownChanged)
    Q_PROPERTY(int quality READ quality NOTIFY qualityChanged)

public:
    explicit ConnectionViewModel(QObject *parent = nullptr);
    ~ConnectionViewModel() override;

    QString partnerId() const { return m_partnerId; }
    void setPartnerId(const QString &id);
    QString password() const { return m_password; }
    void setPassword(const QString &password);
    bool hasPassword() const { return !m_password.isEmpty(); }

    QString stateName() const;
    QString stateDisplayName() const;
    int stateCode() const;
    bool isConnected() const { return m_session->isConnected(); }
    bool isBusy() const;
    bool canConnect() const;
    QString statusText() const;
    QString errorText() const { return m_errorText; }
    QString noticeText() const { return m_noticeText; }
    /// Full diagnostic log (version, state timeline, notices and complete error
    /// details); shown by the log dialog and copied by `copyLogToClipboard`.
    QString logText() const;
    int logCount() const { return int(m_log.size()); }
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

    // --- session tools ------------------------------------------------------
    /// Chat transcript, oldest first: {text, outgoing, system, time}.
    QVariantList chatLog() const { return m_chatLog; }
    int chatUnread() const { return m_chatUnread; }
    bool partnerTyping() const { return m_partnerTyping; }
    /// File transfer rows (see transfer::FileTransferService::rows()).
    QVariantList transfers() const { return m_transfers; }
    int transferActiveCount() const { return m_transferActiveCount; }
    bool transferPending() const { return m_transferPending; }
    QString downloadDirectory() const;
    bool audioEnabled() const { return m_audioEnabled; }
    bool audioAvailable() const { return m_audioAvailable; }
    QString audioDescription() const { return m_audioDescription; }
    bool viewOnly() const { return m_viewOnly; }
    bool wallpaperShown() const { return m_wallpaperShown; }
    int quality() const { return m_quality; }

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
    /// Copy the diagnostic log to the platform clipboard.
    Q_INVOKABLE bool copyLogToClipboard();
    /// Credential-free diagnostics text for the About dialog.
    QString diagnostics() const;
    /// Converts a QML file dialog URL into a local path (identity for plain paths).
    Q_INVOKABLE QString localFileFromUrl(const QString &url) const;
    /// {label, value} rows for the statistics panel.
    QVariantList statisticsRows() const;
    /// Re-read the settings (the settings dialog calls this after editing).
    void settingsApplied();

    // --- session tools ------------------------------------------------------
    /// Append an outgoing chat line and send command 12.
    Q_INVOKABLE bool sendChatMessage(const QString &text);
    /// Mark the chat as read (the chat panel became visible).
    Q_INVOKABLE void markChatRead();
    Q_INVOKABLE void clearChat();
    /// Commands 29 / 30: tell the partner this user is typing.
    Q_INVOKABLE void notifyTyping(bool typing);
    /// Command 46 and the persisted preference.
    Q_INVOKABLE void setAudioEnabled(bool enabled);
    Q_INVOKABLE void toggleAudio();
    /// Command 13: offer a local file.  Takes a QML file-dialog URL or a path.
    Q_INVOKABLE bool offerFile(const QString &url, QString *error = nullptr);
    Q_INVOKABLE void acceptTransfer(const QString &transferId);
    Q_INVOKABLE void declineTransfer(const QString &transferId);
    Q_INVOKABLE void cancelTransfer(const QString &transferId);
    Q_INVOKABLE void cancelAllTransfers();
    /// Commands 22 / 40 / 53.
    Q_INVOKABLE void requestCtrlAltDel();
    Q_INVOKABLE void requestReboot();
    Q_INVOKABLE void requestLock();
    /// Command 23.
    Q_INVOKABLE void setWallpaperShown(bool shown);
    /// Command 31 (30 = speed, 45 = balanced, 70 = quality).
    Q_INVOKABLE void setQuality(int value);

signals:
    void partnerIdChanged();
    void passwordChanged();
    void stateChanged();
    void statusTextChanged();
    void errorTextChanged();
    void noticeTextChanged();
    void logChanged();
    void statisticsChanged();
    void settingsChanged();
    void inputEnabledChanged();
    void screenChanged();
    void chatChanged();
    void typingChanged();
    void transfersChanged();
    void audioChanged();
    void viewOnlyChanged();
    void wallpaperShownChanged();
    void qualityChanged();
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

    void appendLog(const QString &level, const QString &message, const QString &detail = {});
    void applyFeatureSettings();
    void appendChat(const QString &text, bool outgoing, bool system = false);

    struct LogEntry {
        QDateTime time;
        QString level;
        QString message;
        QString detail;
    };

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
    QList<LogEntry> m_log;
    QString m_statusText;
    network::SessionStatistics m_statistics;
    bool m_dictionaryLoaded = false;
    bool m_inputEnabled = true;

    // Session tools.  The worker owns the authoritative state; these are the last
    // values it reported, cached so the UI can bind to plain properties.
    QVariantList m_chatLog;
    int m_chatUnread = 0;
    bool m_partnerTyping = false;
    bool m_localTyping = false;
    QVariantList m_transfers;
    int m_transferActiveCount = 0;
    bool m_transferPending = false;
    bool m_audioEnabled = false;
    bool m_audioAvailable = false;
    QString m_audioDescription;
    bool m_viewOnly = false;
    bool m_wallpaperShown = true;
    int m_quality = 45;

    // Remote cursor state is cached so a screen item attached after the first
    // selection still starts with the right shape and position.
    QImage m_cursorImage;
    QPoint m_cursorHotspot;
    QPoint m_cursorPosition;
    bool m_cursorPositionValid = false;

    // Feature preferences are forwarded to the worker when they change.  The
    // flags avoid re-sending a command that did not change, and force one full
    // send at the start of each session.
    bool m_featureSettingsApplied = false;
    bool m_appliedCursorTracking = false;
    bool m_appliedTransferAutoAccept = false;

    platform::ClipboardBridge *m_clipboard = nullptr;
};

} // namespace ultraview::models
