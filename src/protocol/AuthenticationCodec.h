#pragma once
//
// Partner authentication codec: command 8 (connection request) and the `login:`
// frame that the relay channel carries.
//
// Ported from `uv_reference.py`: E162 (`connection_request_password_value`),
// E163/E164/E165 (`build_connection_request_command`, P2P suffix),
// E168 (`login_numeric_salt_digest`), R191 (`login_password_fields`),
// E185 (`build_login_command`).  Sensitive values only ever live in these
// structures; they are never logged (see `LoginPasswordFields`).
//
#include <QByteArray>
#include <QString>
#include <QStringList>

#include "protocol/AnsiCodec.h"

#include <cstdint>
#include <optional>

namespace ultraview::protocol {

/// Sensitive login positions 0, 1 and 12 of the 22-field login schema.
/// The type deliberately has no `operator<<` and no debug output.
struct LoginPasswordFields {
    QString salt;
    QString primary;
    QString secondary;
};

/// Values of the 17-term command 8 template.  Terms 2, 6 and 7 are computed by
/// the client (password value, controller id, request digest); every other term
/// is explicit user/profile data -- the reference never invents defaults.
struct ConnectionRequestContext {
    QStringList templateTerms;
    QString password;
    qint32 passwordType = 0;
    AnsiCodec codec = AnsiCodec::cp1252();
    /// `registrationProfile.terms[2]`, used for the request digest.
    QString registrationTerm2;
    /// Value substituted into term 1; empty means "keep the template value".
    QString partnerId;
};

class AuthenticationCodec {
public:
    /// `connection_request_password_value`: type 0 hashes ANSI(32 spaces + password)
    /// as lowercase MD5 hex; any other type reuses the string as-is.
    static QString connectionRequestPasswordValue(const QString &password, qint32 passwordType,
                                                  const AnsiCodec &codec);

    /// `login_numeric_salt_digest`: MD5(ANSI(salt + password)), salt must be
    /// exactly eight ASCII digits.
    static QString loginNumericSaltDigest(const QString &password, const QString &salt,
                                          const AnsiCodec &codec);

    /// `login_password_fields` (R191).  Type 0 requires an explicit salt.
    static LoginPasswordFields loginPasswordFields(const QString &password, qint32 passwordType,
                                                   const std::optional<QString> &salt,
                                                   const AnsiCodec &codec);

    /// 22 fields of the login schema, filled at positions 0, 1 and 12.
    static QStringList fillLoginFields(const QStringList &fields, const QString &password,
                                       qint32 passwordType, const std::optional<QString> &salt,
                                       const AnsiCodec &codec);

    /// `build_login_command`: "login:" + 22 ':' joined fields, UTF-16LE, no BOM.
    static QByteArray buildLoginCommand(const QStringList &fields);

    /// `build_login_with_password`: `fillLoginFields` followed by `buildLoginCommand`.
    static QByteArray buildLoginWithPassword(const QStringList &fields, const QString &password,
                                             qint32 passwordType, const std::optional<QString> &salt,
                                             const AnsiCodec &codec);

    /// `build_connection_request_command`: exactly 17 terms, all ':'/NUL free.
    static QByteArray buildConnectionRequestCommand(const QStringList &terms, const AnsiCodec &codec);

    /// Resolve the 17 command-8 terms from a profile template: term 2 (password
    /// value), term 6 (controller id) and term 7 (request digest) are computed,
    /// term 1 (partner id) is overridden when `partnerId` is not empty.
    static QStringList resolveConnectionRequestTerms(const ConnectionRequestContext &context,
                                                     qint32 controllerId);

    /// `connection_request_p2p_suffix`: pipe-joined fields, or empty when P2P is
    /// disabled or the public IP is empty.  Delimiters inside the fields are
    /// rejected (reconstruction policy of the reference).
    static QString connectionRequestP2pSuffix(bool enabled, const QString &publicIp,
                                              const QString &publicSourcePort,
                                              const QString &localIp, const QString &localPort,
                                              const QString &encryptionKey);

    /// Cryptographically strong decimal salt (the reference uses
    /// `secrets.randbelow(10)` per digit; `QRandomGenerator::system()` is the
    /// CSPRNG of the platform).
    static QString randomDigits(int count);

    /// Minimum version the recovered login advertises (6.6.133 in the artifact).
    static QString clientVersion();

    /// Default 22-field login schema used by the reference view:
    ///   ["", "", "1280", "720", "30", "15", "15", "0", "0", <controllerId>,
    ///    <hostname>, "0", "", "", <16 random digits>, "2", "80", "0", "0",
    ///    <partner secret token>, <client version>, "0"]
    /// `viewportWidth`/`viewportHeight` replace the reference's hard-coded
    /// "1280"/"720" negotiation hint; the partner's real desktop size is read from
    /// the login acknowledgement (see ScreenDecoder::desktopSizeFromLogin).
    static QStringList defaultLoginFields(qint32 controllerId, const QString &hostName,
                                          const QString &partnerSecretToken, int viewportWidth,
                                          int viewportHeight);
};

} // namespace ultraview::protocol
