//
// Tests for models/ProfileLoader: the only path between a user-editable JSON
// profile and a session request.
//
// The constraints under test are the recovered schema rules that the Python
// reference also applies (`build_registration_command`,
// `build_connection_request_command`): term counts, the fixed terms, delimiter
// limits, the three decimal tick strings and the command-8 discriminator.
//
// Every profile here is synthetic and lives in a temporary directory; no real
// registration identity is used or committed.
//
#include "core/ErrorTypes.h"
#include "models/ProfileLoader.h"

#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace ultraview;
using namespace ultraview::models;

namespace {

/// Schema-valid registration terms with obvious synthetic values; only the shape
/// matters to the loader.
QStringList syntheticRegistrationTerms()
{
    QStringList terms(26);
    terms[0] = QStringLiteral("3");
    terms[1] = QString(32, QLatin1Char('A'));  // synthetic fingerprint digest
    terms[2] = QStringLiteral("12345678");     // synthetic client password value
    terms[3] = QStringLiteral("0");
    terms[5] = QStringLiteral("0");
    terms[6] = QStringLiteral("dGVzdC1ob3N0"); // base64("test-host")
    terms[7] = QStringLiteral("6.6.133");
    terms[8] = QStringLiteral("AABBCCDDEEFF");
    terms[12] = QString();
    terms[15] = QStringLiteral("en");
    terms[16] = QStringLiteral("tester");
    terms[19] = QStringLiteral("0");
    terms[21] = QStringLiteral("1");
    terms[22] = QStringLiteral("0:0:0");
    return terms;
}

QJsonArray termArray(const QStringList &terms)
{
    QJsonArray array;
    for (const QString &term : terms)
        array.append(term);
    return array;
}

QJsonObject registrationDocument(const QStringList &terms, const QString &codec = QStringLiteral("cp1252"),
                                 bool experimental = true)
{
    QJsonObject object;
    object.insert(QStringLiteral("terms"), termArray(terms));
    object.insert(QStringLiteral("ansi_codec"), codec);
    if (experimental)
        object.insert(QStringLiteral("experimental_linux_metadata_policy"), true);
    return object;
}

QJsonObject partnerDocument(const QStringList &terms, const QString &password = QString(),
                            int passwordType = 0)
{
    QJsonObject object;
    object.insert(QStringLiteral("request_terms"), termArray(terms));
    object.insert(QStringLiteral("password"), password);
    object.insert(QStringLiteral("password_type"), passwordType);
    return object;
}

QString writeJson(QTemporaryDir &directory, const QString &name, const QJsonObject &document)
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QString();
    file.write(QJsonDocument(document).toJson(QJsonDocument::Indented));
    file.close();
    return path;
}

/// Runs a load attempt and reports its failure classification; ErrorCode::None
/// means the load unexpectedly succeeded.
template <typename Function>
ErrorCode loadFailure(Function &&load)
{
    try {
        load();
    } catch (const UvException &exception) {
        return exception.code();
    } catch (...) {
        return ErrorCode::None;
    }
    return ErrorCode::None;
}

} // namespace

class TestProfiles : public QObject {
    Q_OBJECT

private slots:
    void registrationProfileLoads();
    void registrationProfileRejectsMalformedInput();
    void partnerProfileLoads();
    void partnerProfileRejectsMalformedInput();
    void defaultRequestTermsShape();
    void templateWritersRoundTrip();
};

void TestProfiles::registrationProfileLoads()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList terms = syntheticRegistrationTerms();
    const QString path = writeJson(directory, QStringLiteral("registration.json"), registrationDocument(terms));
    QVERIFY(!path.isEmpty());

    const RegistrationProfile profile = ProfileLoader::loadRegistrationProfile(path);
    QVERIFY(profile.isValid());
    QCOMPARE(profile.terms, terms);
    QCOMPARE(profile.codec.name(), QStringLiteral("cp1252"));
    QVERIFY(profile.experimentalLinuxMetadataPolicy);
    QCOMPARE(profile.sourcePath, path);
    const QString description = ProfileLoader::describe(profile);
    QVERIFY(description.contains(QStringLiteral("26 terms")));
    QVERIFY(description.contains(QStringLiteral("cp1252")));
    QVERIFY(description.contains(QStringLiteral("experimental Linux metadata")));

    // Without the experimental flag the profile still loads; the summary simply
    // drops the marker.
    const QString plain = writeJson(directory, QStringLiteral("plain.json"),
                                    registrationDocument(terms, QStringLiteral("utf-8"), false));
    const RegistrationProfile plainProfile = ProfileLoader::loadRegistrationProfile(plain);
    QVERIFY(!plainProfile.experimentalLinuxMetadataPolicy);
    QCOMPARE(plainProfile.codec.name(), QStringLiteral("utf-8"));
    QVERIFY(!ProfileLoader::describe(plainProfile).contains(QStringLiteral("experimental")));
}

void TestProfiles::registrationProfileRejectsMalformedInput()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto expectRejected = [&directory](const QJsonObject &document, const char *what) {
        const QString path = writeJson(directory, QStringLiteral("case.json"), document);
        QVERIFY2(!path.isEmpty(), what);
        QCOMPARE(static_cast<int>(loadFailure([&path] { ProfileLoader::loadRegistrationProfile(path); })),
                 static_cast<int>(ErrorCode::Configuration));
    };

    QStringList terms = syntheticRegistrationTerms();
    terms.removeLast();
    expectRejected(registrationDocument(terms), "25 terms");

    terms = syntheticRegistrationTerms();
    terms.append(QString());
    expectRejected(registrationDocument(terms), "27 terms");

    terms = syntheticRegistrationTerms();
    terms[0] = QStringLiteral("4");
    expectRejected(registrationDocument(terms), "wrong command discriminator");

    terms = syntheticRegistrationTerms();
    terms[3] = QStringLiteral("1");
    expectRejected(registrationDocument(terms), "fixed term 3");

    terms = syntheticRegistrationTerms();
    terms[5] = QStringLiteral("1");
    expectRejected(registrationDocument(terms), "fixed term 5");

    terms = syntheticRegistrationTerms();
    terms[12] = QStringLiteral("x");
    expectRejected(registrationDocument(terms), "fixed term 12");

    terms = syntheticRegistrationTerms();
    terms[6] = QStringLiteral("a:b");
    expectRejected(registrationDocument(terms), "colon outside term 22");

    terms = syntheticRegistrationTerms();
    terms[9] = QString::fromLatin1("a\0b", 3);
    expectRejected(registrationDocument(terms), "NUL byte");

    terms = syntheticRegistrationTerms();
    terms[22] = QStringLiteral("0:0");
    expectRejected(registrationDocument(terms), "two tick strings");

    terms = syntheticRegistrationTerms();
    terms[22] = QStringLiteral("0:a:0");
    expectRejected(registrationDocument(terms), "non-decimal tick");

    terms = syntheticRegistrationTerms();
    terms[22] = QStringLiteral("0::0");
    expectRejected(registrationDocument(terms), "empty tick");

    expectRejected(registrationDocument(syntheticRegistrationTerms(), QStringLiteral("cp932")),
                   "unsupported codec");

    QJsonObject missingCodec = registrationDocument(syntheticRegistrationTerms());
    missingCodec.remove(QStringLiteral("ansi_codec"));
    expectRejected(missingCodec, "missing codec name");

    QJsonObject notString = registrationDocument(syntheticRegistrationTerms());
    QJsonArray mixed = notString.value(QStringLiteral("terms")).toArray();
    mixed.replace(7, 42);
    notString.insert(QStringLiteral("terms"), mixed);
    expectRejected(notString, "non-string term");

    // A file that is not a JSON object at all.
    const QString arrayPath = directory.filePath(QStringLiteral("array.json"));
    QFile arrayFile(arrayPath);
    QVERIFY(arrayFile.open(QIODevice::WriteOnly));
    arrayFile.write("[]");
    arrayFile.close();
    QCOMPARE(static_cast<int>(loadFailure([&arrayPath] { ProfileLoader::loadRegistrationProfile(arrayPath); })),
             static_cast<int>(ErrorCode::Configuration));

    // A missing file is refused, never treated as empty.
    QCOMPARE(static_cast<int>(loadFailure([&directory] {
                 ProfileLoader::loadRegistrationProfile(directory.filePath(QStringLiteral("absent.json")));
             })),
             static_cast<int>(ErrorCode::Configuration));
}

void TestProfiles::partnerProfileLoads()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList terms = ProfileLoader::defaultRequestTerms(QStringLiteral("123456789"));
    const QString path = writeJson(directory, QStringLiteral("partner.json"),
                                   partnerDocument(terms, QStringLiteral("synthetic-password")));
    const PartnerProfile profile = ProfileLoader::loadPartnerProfile(path);
    QVERIFY(profile.isValid());
    QCOMPARE(profile.requestTerms, terms);
    QCOMPARE(profile.password, QStringLiteral("synthetic-password"));
    QCOMPARE(profile.passwordType, 0);
    QVERIFY(ProfileLoader::describe(profile).contains(QStringLiteral("password set")));

    // An empty template password is allowed for type 0: the connect page supplies
    // the real one and the session refuses to start without it.
    const QString empty = writeJson(directory, QStringLiteral("empty.json"), partnerDocument(terms));
    const PartnerProfile emptyProfile = ProfileLoader::loadPartnerProfile(empty);
    QVERIFY(emptyProfile.password.isEmpty());
    QVERIFY(ProfileLoader::describe(emptyProfile).contains(QStringLiteral("password empty")));
}

void TestProfiles::partnerProfileRejectsMalformedInput()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto expectRejected = [&directory](const QJsonObject &document, const char *what) {
        const QString path = writeJson(directory, QStringLiteral("case.json"), document);
        QVERIFY2(!path.isEmpty(), what);
        QCOMPARE(static_cast<int>(loadFailure([&path] { ProfileLoader::loadPartnerProfile(path); })),
                 static_cast<int>(ErrorCode::Configuration));
    };

    QStringList terms = ProfileLoader::defaultRequestTerms(QStringLiteral("123456789"));
    terms.removeLast();
    expectRejected(partnerDocument(terms), "16 terms");

    terms = ProfileLoader::defaultRequestTerms(QStringLiteral("123456789"));
    terms[0] = QStringLiteral("7");
    expectRejected(partnerDocument(terms), "wrong command discriminator");

    expectRejected(partnerDocument(ProfileLoader::defaultRequestTerms(), QString(), 5),
                   "empty password with a non-zero password type");

    QJsonObject missing = partnerDocument(ProfileLoader::defaultRequestTerms());
    missing.remove(QStringLiteral("request_terms"));
    expectRejected(missing, "missing request terms");
}

void TestProfiles::defaultRequestTermsShape()
{
    const QStringList terms = ProfileLoader::defaultRequestTerms(QStringLiteral("42"));
    QCOMPARE(terms.size(), 17);
    QCOMPARE(terms.at(0), QStringLiteral("8"));
    QCOMPARE(terms.at(1), QStringLiteral("42"));
    // Computed by the client at request time, never stored in the template.
    QVERIFY(terms.at(2).isEmpty());
    QVERIFY(terms.at(6).isEmpty());
    QVERIFY(terms.at(7).isEmpty());
    // P2P stays disabled: the session refuses a nonempty suffix.
    QVERIFY(terms.at(16).isEmpty());

    const QJsonObject document = QJsonDocument::fromJson(ProfileLoader::defaultRequestTemplateJson()).object();
    QCOMPARE(document.value(QStringLiteral("password_type")).toInt(0), 0);
    QVERIFY(document.value(QStringLiteral("password")).toString().isEmpty());
    const QJsonArray array = document.value(QStringLiteral("request_terms")).toArray();
    QCOMPARE(array.size(), 17);
    QStringList parsed;
    for (const QJsonValue &value : array)
        parsed.append(value.toString());
    QCOMPARE(parsed, ProfileLoader::defaultRequestTerms());
}

void TestProfiles::templateWritersRoundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString error;

    // Partner template: written for the user, reloaded by the session.
    const QString partnerPath = directory.filePath(QStringLiteral("partner-template.json"));
    QVERIFY2(ProfileLoader::writePartnerTemplate(partnerPath, QStringLiteral("123456789"), false, &error),
             qPrintable(error));
    const PartnerProfile partner = ProfileLoader::loadPartnerProfile(partnerPath);
    QCOMPARE(partner.requestTerms.size(), 17);
    QCOMPARE(partner.requestTerms.at(1), QStringLiteral("123456789"));
    QVERIFY(partner.password.isEmpty());

    // Refusing to overwrite protects user edits; forcing is explicit.
    QVERIFY(!ProfileLoader::writePartnerTemplate(partnerPath, QStringLiteral("1"), false, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(ProfileLoader::writePartnerTemplate(partnerPath, QStringLiteral("1"), true, &error));
    QCOMPARE(ProfileLoader::loadPartnerProfile(partnerPath).requestTerms.at(1), QStringLiteral("1"));

    // Registration scaffold: fixed terms only and still schema valid.
    const QString registrationPath = directory.filePath(QStringLiteral("registration-template.json"));
    QVERIFY2(ProfileLoader::writeRegistrationTemplate(registrationPath, QStringLiteral("cp1252"), false, &error),
             qPrintable(error));
    const RegistrationProfile registration = ProfileLoader::loadRegistrationProfile(registrationPath);
    QCOMPARE(registration.terms.size(), 26);
    QCOMPARE(registration.terms.at(0), QStringLiteral("3"));
    QCOMPARE(registration.terms.at(22), QStringLiteral("0:0:0"));
    QVERIFY(registration.terms.at(1).isEmpty());
    QVERIFY(registration.experimentalLinuxMetadataPolicy);
    QVERIFY(!ProfileLoader::writeRegistrationTemplate(registrationPath, QStringLiteral("cp1252"), false, &error));

    // An unsupported codec name is refused before anything is written.
    const QString otherPath = directory.filePath(QStringLiteral("other.json"));
    QVERIFY(!ProfileLoader::writeRegistrationTemplate(otherPath, QStringLiteral("cp932"), false, &error));
    QVERIFY(!QFileInfo::exists(otherPath));
}

QTEST_MAIN(TestProfiles)
#include "tst_profiles.moc"
