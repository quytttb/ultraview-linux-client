#pragma once
//
// Host (sender) identity.
//
// Hosting needs its own registration identity: the service assigns the public
// ID from the registration's hardware MAC and password, and a viewer
// registration must not take that ID over.  The profile is therefore kept apart
// from the controller's 26-term registration profile.
//
// The file contains the stable 8-digit host password and is written with mode
// 0600 inside a 0700 directory.  It is never logged, copied into diagnostics or
// included in the package.
//
#include <QString>
#include <QStringList>

namespace ultraview::host {

struct HostIdentity {
    QString deviceIdentity; ///< 32 uppercase hex characters, stable per install
    QString password;       ///< 8 digits, shown to the local user and the viewer
    QString displayName;    ///< computer name shown in the viewer
    QString localAddress;   ///< primary IPv4 address
    QString macAddress;     ///< 12 uppercase hex digits
    QString operatingSystem;///< Build|Major|MajorRevision|Minor|MinorRevision|Revision
    QString userName;       ///< ASCII, or base64 of UTF-8
    QString profilePath;

    /// The 28-field command 3 plaintext used by the host.
    QStringList registrationTerms() const;
    /// Base64(displayName) as sent in registration field 6.
    QString computerNameBase64() const;
    bool isValid() const;
};

class HostProfile {
public:
    /// Load the stable identity or create one, then refresh the machine
    /// metadata (address, MAC, OS, user name).  Never overwrites an existing,
    /// valid identity.  Errors are credential-free.
    static HostIdentity loadOrCreate(QString *error);

    /// Same as loadOrCreate() for an explicit profile path.  Used by tests and
    /// by callers that must keep the identity outside the default location.
    static HostIdentity loadOrCreateAt(const QString &path, QString *error);

    /// Default profile location (`<AppConfig>/host-profile.json`).
    static QString defaultPath();

    /// Validate a password accepted by the host login codec: printable ASCII,
    /// 8..256 characters.
    static bool isAcceptablePassword(const QString &password);

    /// Format `Build|Major|MajorRevision|Minor|MinorRevision|Revision` from a
    /// kernel release such as "6.17.0-14-generic".
    static QString operatingSystemWireValue(const QString &kernelRelease);

    /// Normalise "aa:bb:cc:dd:ee:ff" to "AABBCCDDEEFF"; rejects multicast,
    /// all-zero and the all-zero locally-administered placeholder.
    static QString normalizeMac(const QString &text);

    /// Stable locally administered fallback derived from the device identity.
    static QString fallbackMac(const QString &deviceIdentity);

private:
    static QString primaryInterface();
    static QString primaryAddress(const QString &interfaceName);
    static QString primaryMac(const QString &interfaceName);
};

} // namespace ultraview::host
