#pragma once
//
// Registration-channel codec (command 3 "InitMyPassword" and its reply).
//
// Ported from `uv_reference.py` (RC40 1485/1486/1487, E130/E131/E147) and
// `registration_transport.py`.  Everything that touches the wire is exactly the
// reference's representation; the only additions are checked limits and explicit
// errors, which are marked as such.
//
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include "protocol/AnsiCodec.h"

#include <cstdint>

namespace ultraview::protocol {

/// Identity subset of a plaintext command 3 reply (`Command3Identity`).
struct Command3Identity {
    qint32 clientId = 0;
    quint16 publicSourcePort = 0;
    QString publicIp;
};

/// The five characters hard-coded in the recovered startup call (5/10/15/20/25).
/// This is a *fixed protocol constant extracted from the artifact*, not a
/// credential of this project: it is the obfuscation key of the registration
/// channel, identical for every installation of the original software.
QByteArray registrationPassword();

class RegistrationCodec {
public:
    /// `derive_vfunction_key_bytes`: SHA1(UTF-16LE(password)) resized with zero
    /// extension/truncation to `length` bytes.
    static QByteArray deriveVFunctionKeyBytes(const QString &password, int length);

    /// TripleDES-CBC/PKCS7 with the derived key/IV (.NET Framework defaults).
    /// Throws ErrorCode::Protocol when the ciphertext is not a whole number of
    /// blocks or the padding is invalid.
    static QByteArray encryptRegistrationBytes(const QByteArray &payload,
                                               const QByteArray &password = registrationPassword());
    static QByteArray decryptRegistrationBytes(const QByteArray &ciphertext,
                                               const QByteArray &password = registrationPassword());

    /// `registration_fingerprint_digest`: lowercase MD5 hex, uppercased.
    static QString fingerprintDigest(const QByteArray &preimage);

    /// Observed startup fallback: MD5 of the ANSI concatenation of the three
    /// machine strings, with **no separator** (63c66b..63c7a5).
    static QString fallbackFingerprint(const QString &macAddress,
                                       const QString &programFilesCreationTicks,
                                       const QString &systemDirectoryCreationTicks,
                                       const AnsiCodec &codec);

    /// Build the 26-term command 3 payload.  Validation matches
    /// `build_registration_command` term by term: term 0 == "3", term 3 == "0",
    /// term 5 == "0", term 12 == "", term 22 must be three unsigned decimal tick
    /// strings joined by ':', and no other term may contain ':' or NUL.
    static QByteArray buildRegistrationCommand(const QStringList &terms, const AnsiCodec &codec);

    /// Check the 26-term schema without building the payload (UI/profile
    /// validation).  Returns an empty string when the terms are acceptable,
    /// otherwise a human-readable, credential-free reason.
    static QString validateRegistrationTerms(const QStringList &terms);

    /// Parse a command 3 reply.  Accepts only ASCII decimal numeric fields within
    /// the modelled ranges, exactly like `extract_command3_identity`.
    static Command3Identity parseCommand3Identity(const QByteArray &payload, const AnsiCodec &codec);

    /// `read_referral_int32`: first four bytes of Referral.ini as Int32LE.
    static qint32 readReferralInt32(const QByteArray &data);

    /// `format_os_version_combine`: "Build|Major|MajorRevision|Minor|MinorRevision|Revision"
    /// with the two signed Int16 halves of `revision`.
    static QString formatOsVersionCombine(qint32 major, qint32 minor, qint32 build, qint32 revision);

    /// `version_equal_or_greater`: compares the first three numeric components.
    /// A current version with fewer than three components returns false without
    /// raising; invalid digits raise ErrorCode::Protocol, as in the reference.
    static bool versionEqualOrGreater(const QString &current, const QString &minimum);
};

} // namespace ultraview::protocol
