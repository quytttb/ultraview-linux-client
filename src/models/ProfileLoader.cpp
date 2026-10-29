#include "models/ProfileLoader.h"

#include "core/ErrorTypes.h"
#include "protocol/RegistrationCodec.h"

#include <QFile>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace ultraview::models {
namespace {

QJsonObject readObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throwError(ErrorCode::Configuration, QCoreApplication::translate("ProfileLoader", "cannot read the profile file"), path);
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull() || !document.isObject()) {
        throwError(ErrorCode::Configuration, QCoreApplication::translate("ProfileLoader", "the profile is not a JSON object"),
                   parseError.errorString());
    }
    return document.object();
}

QStringList readStringArray(const QJsonObject &object, const QString &key, int expectedSize,
                            const QString &path)
{
    const QJsonValue value = object.value(key);
    if (!value.isArray())
        throwError(ErrorCode::Configuration, QCoreApplication::translate("ProfileLoader", "the profile has no \"%1\" array").arg(key), path);
    const QJsonArray array = value.toArray();
    if (expectedSize > 0 && array.size() != expectedSize) {
        throwError(ErrorCode::Configuration,
                   QCoreApplication::translate("ProfileLoader", "\"%1\" must contain exactly %2 strings").arg(key).arg(expectedSize),
                   QCoreApplication::translate("ProfileLoader", "%1 has %2").arg(QFileInfo(path).fileName()).arg(array.size()));
    }
    QStringList terms;
    terms.reserve(array.size());
    for (const QJsonValue &element : array) {
        if (!element.isString())
            throwError(ErrorCode::Configuration, QCoreApplication::translate("ProfileLoader", "\"%1\" must only contain strings").arg(key));
        terms.append(element.toString());
    }
    return terms;
}

protocol::AnsiCodec readCodec(const QJsonObject &object)
{
    const QString name = object.value(QStringLiteral("ansi_codec")).toString();
    if (name.isEmpty())
        throwError(ErrorCode::Configuration,
                   QCoreApplication::translate("ProfileLoader", "the registration profile needs an \"ansi_codec\" name"));
    const auto codec = protocol::AnsiCodec::fromName(name);
    if (!codec.has_value()) {
        throwError(ErrorCode::Configuration, QCoreApplication::translate("ProfileLoader", "unsupported ansi_codec"),
                   QCoreApplication::translate("ProfileLoader", "supported: cp1252, latin1, ascii, utf-8"));
    }
    return *codec;
}

} // namespace

RegistrationProfile ProfileLoader::loadRegistrationProfile(const QString &path)
{
    const QJsonObject object = readObject(path);
    RegistrationProfile profile;
    profile.sourcePath = path;
    profile.terms = readStringArray(object, QStringLiteral("terms"), 26, path);
    profile.codec = readCodec(object);
    profile.experimentalLinuxMetadataPolicy =
        object.value(QStringLiteral("experimental_linux_metadata_policy")).toBool(false);
    const QString reason = protocol::RegistrationCodec::validateRegistrationTerms(profile.terms);
    if (!reason.isEmpty()) {
        throwError(ErrorCode::Configuration,
                   QCoreApplication::translate("ProfileLoader", "the registration profile does not satisfy the recovered schema"), reason);
    }
    return profile;
}

PartnerProfile ProfileLoader::loadPartnerProfile(const QString &path)
{
    const QJsonObject object = readObject(path);
    PartnerProfile profile;
    profile.sourcePath = path;
    profile.requestTerms = readStringArray(object, QStringLiteral("request_terms"), 17, path);
    profile.password = object.value(QStringLiteral("password")).toString();
    profile.passwordType = static_cast<qint32>(object.value(QStringLiteral("password_type")).toInt(0));
    if (profile.requestTerms.value(0) != QLatin1String("8"))
        throwError(ErrorCode::Configuration, QCoreApplication::translate("ProfileLoader", "request_terms must begin with \"8\""));
    if (profile.passwordType != 0 && profile.password.isEmpty())
        throwError(ErrorCode::Configuration, QCoreApplication::translate("ProfileLoader", "the partner password is empty"));
    return profile;
}

QStringList ProfileLoader::defaultRequestTerms(const QString &partnerId)
{
    // Values observed in the reference `default-partner.json` profile, with the
    // partner id replaced by the one the user types.  Empty terms are the ones the
    // reference itself leaves unresolved (they are filled by the client at request
    // time: password value, controller id, request digest, P2P suffix).
    return QStringList{
        QStringLiteral("8"),                       // 0  command
        partnerId,                                 // 1  target partner id
        QString(),                                 // 2  password value (computed)
        QStringLiteral("6.6.133"),                 // 3  client version
        QString(),                                 // 4
        QString(),                                 // 5
        QString(),                                 // 6  controller id (computed)
        QString(),                                 // 7  request digest (computed)
        QStringLiteral("-1"),                      // 8
        QStringLiteral("0"),                       // 9
        QStringLiteral("0"),                       // 10
        QStringLiteral("0"),                       // 11
        QString(),                                 // 12
        QStringLiteral("420"),                     // 13
        QStringLiteral("0"),                       // 14
        QString(),                                 // 15
        QString(),                                 // 16 P2P suffix (empty: disabled)
    };
}

QByteArray ProfileLoader::defaultRequestTemplateJson()
{
    QJsonObject object;
    QJsonArray terms;
    for (const QString &term : defaultRequestTerms())
        terms.append(term);
    object.insert(QStringLiteral("request_terms"), terms);
    object.insert(QStringLiteral("password"), QString());
    object.insert(QStringLiteral("password_type"), 0);
    object.insert(QStringLiteral("_comment"),
                  QCoreApplication::translate("ProfileLoader", "Template for the recovered command 8 request. Fill password and, if "
                                 "needed, the empty terms. Field values come from the reference profile; "
                                 "they are not re-derived here."));
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

bool ProfileLoader::writePartnerTemplate(const QString &path, const QString &partnerId, bool overwrite,
                                        QString *error)
{
    if (!overwrite && QFileInfo::exists(path)) {
        if (error)
            *error = QCoreApplication::translate("ProfileLoader", "the file already exists");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QCoreApplication::translate("ProfileLoader", "cannot open the file for writing");
        return false;
    }
    QJsonObject object;
    QJsonArray terms;
    for (const QString &term : defaultRequestTerms(partnerId))
        terms.append(term);
    object.insert(QStringLiteral("request_terms"), terms);
    object.insert(QStringLiteral("password"), QString());
    object.insert(QStringLiteral("password_type"), 0);
    object.insert(QStringLiteral("_comment"),
                  QCoreApplication::translate("ProfileLoader", "Partner request template. The client fills terms 2, 6 and 7."));
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QCoreApplication::translate("ProfileLoader", "cannot write the profile file");
        return false;
    }
    return true;
}

bool ProfileLoader::writeRegistrationTemplate(const QString &path, const QString &ansiCodec, bool overwrite,
                                             QString *error)
{
    if (!overwrite && QFileInfo::exists(path)) {
        if (error)
            *error = QCoreApplication::translate("ProfileLoader", "the file already exists");
        return false;
    }
    if (!protocol::AnsiCodec::fromName(ansiCodec).has_value()) {
        if (error)
            *error = QCoreApplication::translate("ProfileLoader", "unsupported ansi_codec");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QCoreApplication::translate("ProfileLoader", "cannot open the file for writing");
        return false;
    }
    // Terms 0/3/5/12 are fixed by the recovered schema; term 22 must hold three
    // unsigned decimal tick strings.  Everything else stays empty for the user to
    // fill from their own profile: this project cannot derive machine fingerprints.
    QStringList terms;
    terms.resize(26);
    terms[0] = QStringLiteral("3");
    terms[3] = QStringLiteral("0");
    terms[5] = QStringLiteral("0");
    terms[12] = QString();
    terms[22] = QStringLiteral("0:0:0");

    QJsonObject object;
    QJsonArray array;
    for (const QString &term : terms)
        array.append(term);
    object.insert(QStringLiteral("terms"), array);
    object.insert(QStringLiteral("ansi_codec"), ansiCodec);
    object.insert(QStringLiteral("experimental_linux_metadata_policy"), true);
    object.insert(QStringLiteral("_comment"),
                  QCoreApplication::translate("ProfileLoader", "Scaffolding only: the machine-specific terms are empty because the "
                                 "fingerprint policy was never recovered. Copy the values from a working "
                                 "reference profile instead of guessing them."));
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QCoreApplication::translate("ProfileLoader", "cannot write the profile file");
        return false;
    }
    return true;
}

QString ProfileLoader::describe(const RegistrationProfile &profile)
{
    if (!profile.isValid())
        return QCoreApplication::translate("ProfileLoader", "no registration profile loaded");
    return QStringLiteral("%1 terms, ansi_codec %2%3")
        .arg(profile.terms.size())
        .arg(profile.codec.name(),
             profile.experimentalLinuxMetadataPolicy ? QStringLiteral(", experimental Linux metadata")
                                                     : QString());
}

QString ProfileLoader::describe(const PartnerProfile &profile)
{
    if (!profile.isValid())
        return QCoreApplication::translate("ProfileLoader", "no partner profile loaded");
    return QStringLiteral("%1 request terms, password %2, type %3")
        .arg(profile.requestTerms.size())
        .arg(profile.password.isEmpty() ? QStringLiteral("empty") : QStringLiteral("set"))
        .arg(profile.passwordType);
}

} // namespace ultraview::models
