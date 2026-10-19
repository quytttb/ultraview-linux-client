#pragma once
//
// Profile loading.
//
// The recovered protocol needs two pieces of *user specific* data that the
// reference implementation also keeps outside the code:
//
//   registration profile  {"terms": [26 strings], "ansi_codec": "cp1252",
//                          "experimental_linux_metadata_policy": bool}
//   partner profile       {"request_terms": [17 strings], "password": str,
//                          "password_type": int}
//
// They are the identities of the *controlling* machine and of the requested
// partner, and they contain per-installation values (a machine fingerprint and the
// client's own password material).  This project therefore never ships them: the
// UI asks for a file and `ProfileLoader` validates it strictly with the same
// rules the reference applies before building a packet.
//
#include <QString>
#include <QStringList>

#include "protocol/AnsiCodec.h"

namespace ultraview::models {

struct RegistrationProfile {
    QStringList terms;
    protocol::AnsiCodec codec = protocol::AnsiCodec::cp1252();
    bool experimentalLinuxMetadataPolicy = false;
    QString sourcePath;

    bool isValid() const { return terms.size() == 26; }
};

struct PartnerProfile {
    QStringList requestTerms;
    QString password;
    qint32 passwordType = 0;
    QString sourcePath;

    bool isValid() const { return requestTerms.size() == 17; }
};

class ProfileLoader {
public:
    /// Load and validate a registration profile.  Throws UvException with
    /// ErrorCode::Configuration/Protocol on malformed input; the error text never
    /// contains a term value.
    static RegistrationProfile loadRegistrationProfile(const QString &path);

    /// Load a partner profile template (17 request terms + password + type).
    static PartnerProfile loadPartnerProfile(const QString &path);

    /// The 17-term command-8 template with the observed defaults of the reference
    /// profile and a placeholder partner id.  Terms 2, 6 and 7 are computed by the
    /// client and are empty here.
    static QStringList defaultRequestTerms(const QString &partnerId = QString());

    /// JSON text of the same template, for "save template" in the UI.
    static QByteArray defaultRequestTemplateJson();

    /// Write a partner template that a user can edit.  Refuses to overwrite an
    /// existing file unless `overwrite` is set.
    static bool writePartnerTemplate(const QString &path, const QString &partnerId, bool overwrite,
                                     QString *error);

    /// Copy the registration template (all 26 terms empty except the fixed ones)
    /// so a user can fill in the recovered machine values.  The values themselves
    /// cannot be generated on Linux -- the reference documents the fingerprint
    /// policy as unresolved -- so this only creates scaffolding.
    static bool writeRegistrationTemplate(const QString &path, const QString &ansiCodec, bool overwrite,
                                          QString *error);

    /// Human readable, credential-free validation summary for the UI.
    static QString describe(const RegistrationProfile &profile);
    static QString describe(const PartnerProfile &profile);
};

} // namespace ultraview::models
