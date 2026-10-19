//
// Parity tests for protocol/RegistrationCodec and protocol/Digest.
//
// Every expected value comes from `tests/data/fixtures.json`, which is generated
// by running the Python reference (`tools/generate_fixtures.py`).
//
#include "TestSupport.h"

#include "core/ErrorTypes.h"
#include "protocol/AnsiCodec.h"
#include "protocol/Digest.h"
#include "protocol/RegistrationCodec.h"

#include <QtTest>

using namespace ultraview;
using namespace ultraview::protocol;

namespace {

const AnsiCodec &cp1252()
{
    static const AnsiCodec codec = AnsiCodec::cp1252();
    return codec;
}

} // namespace

class TestRegistration : public QObject {
    Q_OBJECT

private slots:
    void registrationPassword();
    void vfunctionKeyDerivation();
    void registrationCommand();
    void registrationCommandValidation();
    void command3Identity();
    void invalidCommand3Payloads();
    void registrationCipher();
    void registrationCipherRejectsGarbage();
    void fingerprintDigest();
    void fallbackFingerprint();
    void versionComparison();
    void referralInt32();
    void osVersionCombine();
    void aesRoundTripOfDigestHelpers();
};

void TestRegistration::registrationPassword()
{
    const QJsonArray chars =
        uvtest::Fixtures::instance().object(QStringLiteral("registration_password"))
            .value(QStringLiteral("chars"))
            .toArray();
    QCOMPARE(chars.size(), 5);
    QByteArray expected;
    for (const QJsonValue &value : chars)
        expected.append(static_cast<char>(value.toInt()));
    // Fixed protocol constant extracted from the artifact (not a credential of
    // this project), pinned by the reference fixture.
    QCOMPARE(protocol::registrationPassword(), expected);
}

void TestRegistration::vfunctionKeyDerivation()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("vfunction_key"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        const QString password = entry.value(QStringLiteral("password")).toString();
        const int length = entry.value(QStringLiteral("length")).toInt();
        QCOMPARE(RegistrationCodec::deriveVFunctionKeyBytes(password, length), uvtest::Fixtures::entryBytes(entry));
    }
    QVERIFY_EXCEPTION_THROWN(RegistrationCodec::deriveVFunctionKeyBytes(QStringLiteral("abc"), -1), UvException);
}

void TestRegistration::registrationCommand()
{
    const QJsonObject fixture = uvtest::Fixtures::instance().object(QStringLiteral("registration_command"));
    QStringList terms;
    for (const QJsonValue &value : fixture.value(QStringLiteral("terms")).toArray())
        terms.append(value.toString());
    QCOMPARE(terms.size(), 26);
    QCOMPARE(RegistrationCodec::buildRegistrationCommand(terms, cp1252()), uvtest::Fixtures::entryBytes(fixture));
    QVERIFY(RegistrationCodec::validateRegistrationTerms(terms).isEmpty());

    // The same profile from the shared test data must validate too.
    const QStringList shared = uvtest::testRegistrationTerms();
    QCOMPARE(shared.size(), 26);
    QVERIFY(RegistrationCodec::validateRegistrationTerms(shared).isEmpty());
    QVERIFY(!RegistrationCodec::buildRegistrationCommand(shared, cp1252()).isEmpty());
}

void TestRegistration::registrationCommandValidation()
{
    const QStringList valid = uvtest::testRegistrationTerms();
    QCOMPARE(valid.size(), 26);

    const auto rejects = [](const QStringList &terms) {
        return !RegistrationCodec::validateRegistrationTerms(terms).isEmpty();
    };
    QVERIFY(RegistrationCodec::validateRegistrationTerms(valid).isEmpty());

    QStringList tooShort = valid;
    tooShort.removeLast();
    QVERIFY(rejects(tooShort));

    QStringList wrongCommand = valid;
    wrongCommand[0] = QStringLiteral("4");
    QVERIFY(rejects(wrongCommand));

    QStringList wrongTerm3 = valid;
    wrongTerm3[3] = QStringLiteral("1");
    QVERIFY(rejects(wrongTerm3));

    QStringList wrongTerm5 = valid;
    wrongTerm5[5] = QStringLiteral("1");
    QVERIFY(rejects(wrongTerm5));

    QStringList colonTerm = valid;
    colonTerm[4] = QStringLiteral("a:b");
    QVERIFY(rejects(colonTerm));

    QStringList badTicks = valid;
    badTicks[22] = QStringLiteral("1700000000000:1700000000001");
    QVERIFY(rejects(badTicks));

    QStringList nonNumericTicks = valid;
    nonNumericTicks[22] = QStringLiteral("a:1700000000001:1700000000002");
    QVERIFY(rejects(nonNumericTicks));

    QStringList nullTerm = valid;
    nullTerm[6] = QString(QChar(0));
    QVERIFY(rejects(nullTerm));

    // Template 12 is required to be empty by the recovered schema.
    QStringList nonEmpty12 = valid;
    nonEmpty12[12] = QStringLiteral("x");
    QVERIFY(rejects(nonEmpty12));

    // buildRegistrationCommand performs the same validation.
    QVERIFY_EXCEPTION_THROWN(RegistrationCodec::buildRegistrationCommand(tooShort, cp1252()), UvException);
    QVERIFY_EXCEPTION_THROWN(RegistrationCodec::buildRegistrationCommand(colonTerm, cp1252()), UvException);
}

void TestRegistration::command3Identity()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("command3_payloads"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        const Command3Identity identity =
            RegistrationCodec::parseCommand3Identity(uvtest::Fixtures::entryBytes(entry), cp1252());
        QCOMPARE(identity.clientId, entry.value(QStringLiteral("client_id")).toInt());
        QCOMPARE(identity.publicSourcePort,
                 static_cast<quint16>(entry.value(QStringLiteral("public_source_port")).toInt()));
        QCOMPARE(identity.publicIp, entry.value(QStringLiteral("public_ip")).toString());
    }
}

void TestRegistration::invalidCommand3Payloads()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("invalid_command3_payloads"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QByteArray payload = uvtest::fromHex(value.toString());
        try {
            RegistrationCodec::parseCommand3Identity(payload, cp1252());
            QFAIL("an invalid command 3 payload was accepted");
        } catch (const UvException &exception) {
            QCOMPARE(exception.code(), ErrorCode::Protocol);
            QVERIFY(!exception.message().contains(QLatin1Char('\n')));
        }
    }
}

void TestRegistration::registrationCipher()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("registration_cipher"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        const QByteArray plaintext = uvtest::Fixtures::entryBytes(entry, QStringLiteral("plaintext_hex"));
        const QByteArray ciphertext = uvtest::Fixtures::entryBytes(entry, QStringLiteral("hex"));
        QCOMPARE(RegistrationCodec::encryptRegistrationBytes(plaintext), ciphertext);
        QCOMPARE(RegistrationCodec::decryptRegistrationBytes(ciphertext),
                 uvtest::Fixtures::entryBytes(entry, QStringLiteral("decrypted_hex")));
    }
}

void TestRegistration::registrationCipherRejectsGarbage()
{
    // Not a whole number of blocks.
    QVERIFY_EXCEPTION_THROWN(RegistrationCodec::decryptRegistrationBytes(QByteArrayLiteral("short")),
                             UvException);
    // Whole number of blocks but invalid PKCS#7 padding.
    const QByteArray sixteenZeros(16, '\0');
    try {
        RegistrationCodec::decryptRegistrationBytes(sixteenZeros);
        QFAIL("invalid padding was accepted");
    } catch (const UvException &exception) {
        QCOMPARE(exception.code(), ErrorCode::Protocol);
    }
    // The recovery constant derives the key material; a mismatching key must fail.
    const QByteArray ciphertext =
        RegistrationCodec::encryptRegistrationBytes(QByteArrayLiteral("3:a:b:c"));
    QVERIFY_EXCEPTION_THROWN(RegistrationCodec::decryptRegistrationBytes(ciphertext, QByteArrayLiteral("nope")),
                             UvException);
}

void TestRegistration::fingerprintDigest()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("fingerprint"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        QCOMPARE(RegistrationCodec::fingerprintDigest(
                     uvtest::Fixtures::entryBytes(entry, QStringLiteral("preimage_hex"))),
                 entry.value(QStringLiteral("digest")).toString());
    }
}

void TestRegistration::fallbackFingerprint()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("fallback_fingerprint"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        QCOMPARE(RegistrationCodec::fallbackFingerprint(entry.value(QStringLiteral("mac_address")).toString(),
                                                       entry.value(QStringLiteral("program_files_ticks")).toString(),
                                                       entry.value(QStringLiteral("system_directory_ticks")).toString(),
                                                       cp1252()),
                 entry.value(QStringLiteral("digest")).toString());
    }
}

void TestRegistration::versionComparison()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("version_compare"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        QCOMPARE(RegistrationCodec::versionEqualOrGreater(entry.value(QStringLiteral("current")).toString(),
                                                         entry.value(QStringLiteral("minimum")).toString()),
                 entry.value(QStringLiteral("expected")).toBool());
    }
    // Too few components: false, without raising (pinned by the fixture).
    const QJsonArray shortCases = uvtest::Fixtures::instance().array(QStringLiteral("version_compare_false"));
    for (const QJsonValue &value : shortCases) {
        const QJsonObject entry = value.toObject();
        QVERIFY(!RegistrationCodec::versionEqualOrGreater(entry.value(QStringLiteral("current")).toString(),
                                                         entry.value(QStringLiteral("minimum")).toString()));
    }
    // Non-numeric components are refused with a protocol error.
    QVERIFY_EXCEPTION_THROWN(RegistrationCodec::versionEqualOrGreater(QStringLiteral("a.b.c"),
                                                                      QStringLiteral("6.6.0")),
                             UvException);
}

void TestRegistration::referralInt32()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("referral_int32"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        QCOMPARE(RegistrationCodec::readReferralInt32(uvtest::Fixtures::entryBytes(entry)),
                 entry.value(QStringLiteral("value")).toInt());
    }
    QVERIFY_EXCEPTION_THROWN(RegistrationCodec::readReferralInt32(QByteArrayLiteral("\x01\x02\x03")), UvException);
}

void TestRegistration::osVersionCombine()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("os_version_combine"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        QCOMPARE(RegistrationCodec::formatOsVersionCombine(entry.value(QStringLiteral("major")).toInt(),
                                                          entry.value(QStringLiteral("minor")).toInt(),
                                                          entry.value(QStringLiteral("build")).toInt(),
                                                          entry.value(QStringLiteral("revision")).toInt()),
                 entry.value(QStringLiteral("value")).toString());
    }
}

void TestRegistration::aesRoundTripOfDigestHelpers()
{
    // The digest helpers are used by the P2P key derivation as well; both must
    // agree with the direct OpenSSL result the reference produces.
    const QByteArray utf16 = utf16leBytes(QStringLiteral("abc"));
    QCOMPARE(utf16, QByteArrayLiteral("a\x00" "b\x00" "c\x00"));
    QCOMPARE(md5HexLower(QByteArrayLiteral("abc")), QStringLiteral("900150983cd24fb0d6963f7d28e17f72"));
    QCOMPARE(md5HexUpper(QByteArrayLiteral("abc")), QStringLiteral("900150983CD24FB0D6963F7D28E17F72"));
    QCOMPARE(sha1Bytes(QByteArrayLiteral("abc")).toHex(),
             QByteArrayLiteral("a9993e364706816aba3e25717850c26c9cd0d89d"));
}

QTEST_MAIN(TestRegistration)
#include "tst_registration.moc"
