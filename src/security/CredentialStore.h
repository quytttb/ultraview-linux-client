#pragma once
//
// Credential handling.
//
// Rules this project follows (see docs/compatibility.md, "Secret handling"):
//
//   * passwords are never written to settings, logs, crash reports or the
//     diagnostics panel,
//   * partner IDs, host names and non-secret preferences are stored plainly,
//     because they are not credentials,
//   * passwords may optionally be persisted through the *platform* secret service
//     (libsecret / GNOME keyring / KWallet).  When the build has no secret service,
//     `isPasswordStorageAvailable()` returns false and the UI disables the
//     "remember password" option instead of falling back to a plain file.
//
#include <QHash>
#include <QObject>
#include <QString>

namespace ultraview::security {

class CredentialStore : public QObject {
    Q_OBJECT

public:
    explicit CredentialStore(QObject *parent = nullptr);

    /// True when the platform provides a secret service for this build.
    static bool isPasswordStorageAvailable();

    /// Human readable backend name for the settings dialog.
    static QString backendName();

    /// Store (or replace) the password of `partnerId`.  Returns false and fills
    /// `error` when no secret service is available.
    bool storePassword(const QString &partnerId, const QString &password, QString *error = nullptr);

    /// Retrieve a stored password.  Returns an empty optional when there is none.
    std::optional<QString> password(const QString &partnerId, QString *error = nullptr) const;

    /// Forget a stored password.
    bool forgetPassword(const QString &partnerId, QString *error = nullptr);

    /// Forget every password this client stored.
    bool forgetAll(QString *error = nullptr);

    /// Session-only password (never touches the disk or the keyring).  This is
    /// what the connect dialog uses when "remember" is off.
    void setSessionPassword(const QString &partnerId, const QString &password);
    std::optional<QString> sessionPassword(const QString &partnerId) const;
    void clearSessionPasswords();

private:
    QHash<QString, QString> m_sessionPasswords;
};

} // namespace ultraview::security
