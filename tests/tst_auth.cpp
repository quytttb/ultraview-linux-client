//
// Parity tests for protocol/AuthenticationCodec and protocol/P2pCodec.
//
// The expected values are produced by the Python reference
// (`tools/generate_fixtures.py`); nothing here is derived from the C++ port.
//
#include "TestSupport.h"

#include "core/ErrorTypes.h"
#include "protocol/AnsiCodec.h"
#include "protocol/AuthenticationCodec.h"
#include "protocol/Digest.h"
#include "protocol/P2pCodec.h"

#include <QtTest>

using namespace ultraview;
using namespace ultraview::protocol;

namespace {

QStringList jsonStrings(const QJsonArray &array)
{
    QStringList out;
    out.reserve(array.size());
    for (const QJsonValue &value : array)
        out.append(value.isNull() ? QString() : value.toString());
    return out;
}

} // namespace

class TestAuth : public QObject {
    Q_OBJECT

private slots:
    void requestPasswordValue();
    void loginSaltDigest();
    void loginFieldsAndCommand();
    void loginFieldValidation();
    void connectionRequestCommand();
    void connectionRequestValidation();
    void p2pSuffix();
    void p2pKeyDerivation();
    void p2pEncryption();
    void p2pFrames();
    void p2pTickEntry();
    void initialiserNoop();
};

void TestAuth::requestPasswordValue()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("password_values"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        QCOMPARE(AuthenticationCodec::connectionRequestPasswordValue(
                     entry.value(QStringLiteral("password")).toString(),
                     entry.value(QStringLiteral("password_type")).toInt(), AnsiCodec::cp1252()),
                 entry.value(QStringLiteral("value")).toString());
    }
}

void TestAuth::loginSaltDigest()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("salt_digest"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        QCOMPARE(AuthenticationCodec::loginNumericSaltDigest(entry.value(QStringLiteral("password")).toString(),
                                                            entry.value(QStringLiteral("salt")).toString(),
                                                            AnsiCodec::cp1252()),
                 entry.value(QStringLiteral("digest")).toString());
    }
    // The salt must be exactly eight ASCII digits.
    for (const QString &bad : {QStringLiteral("1234"), QStringLiteral("1234567a"),
                               QStringLiteral("123456789"), QString()}) {
        QVERIFY_EXCEPTION_THROWN(AuthenticationCodec::loginNumericSaltDigest(QStringLiteral("x"), bad,
                                                                             AnsiCodec::cp1252()),
                                 UvException);
    }
}

void TestAuth::loginFieldsAndCommand()
{
    const QJsonObject fixture = uvtest::Fixtures::instance().object(QStringLiteral("login"));
    const QStringList metadata = jsonStrings(fixture.value(QStringLiteral("metadata_fields")).toArray());
    QCOMPARE(metadata.size(), 22);

    const QJsonArray cases = fixture.value(QStringLiteral("cases")).toArray();
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        const QString password = entry.value(QStringLiteral("password")).toString();
        const qint32 passwordType = entry.value(QStringLiteral("password_type")).toInt();
        const std::optional<QString> salt =
            entry.value(QStringLiteral("salt")).isNull()
                ? std::nullopt
                : std::optional<QString>(entry.value(QStringLiteral("salt")).toString());
        const AnsiCodec codec = AnsiCodec::cp1252();

        const QStringList filled =
            AuthenticationCodec::fillLoginFields(metadata, password, passwordType, salt, codec);
        QCOMPARE(filled.size(), 22);
        // Only the recovered positions 0, 1 and 12 may be modified.
        for (int index = 0; index < metadata.size(); ++index) {
            if (index == 0 || index == 1 || index == 12) {
                if (passwordType == 0) {
                    QVERIFY2(filled.at(index) != metadata.at(index) || metadata.at(index).isEmpty(),
                             qPrintable(QStringLiteral("position %1 was not filled").arg(index)));
                }
            } else {
                QCOMPARE(filled.at(index), metadata.at(index));
            }
        }
        const QJsonArray positions = entry.value(QStringLiteral("positions")).toArray();
        for (const QJsonValue &positionValue : positions) {
            const QJsonObject position = positionValue.toObject();
            const int index = position.value(QStringLiteral("index")).toInt();
            QCOMPARE(filled.at(index), position.value(QStringLiteral("value")).toString());
        }

        // The command is "login:" + the fields, UTF-16LE, without a BOM.
        const QByteArray command = AuthenticationCodec::buildLoginCommand(filled);
        QCOMPARE(command, uvtest::Fixtures::entryBytes(entry));
        QCOMPARE(command.left(12), utf16leBytes(QStringLiteral("login:")));
        QCOMPARE(command.size() % 2, 0);
        QCOMPARE(utf16leBytes(filled.join(QLatin1Char(':'))), command.mid(12));
    }
}

void TestAuth::loginFieldValidation()
{
    QStringList metadata;
    for (int index = 0; index < 22; ++index)
        metadata.append(QStringLiteral("m%1").arg(index));

    // Type 0 requires an explicit salt.
    QVERIFY_EXCEPTION_THROWN(AuthenticationCodec::fillLoginFields(metadata, QStringLiteral("p"), 0,
                                                                 std::nullopt, AnsiCodec::cp1252()),
                             UvException);
    // A non-22-field schema is refused.
    QStringList shortSchema = metadata;
    shortSchema.removeLast();
    QVERIFY_EXCEPTION_THROWN(AuthenticationCodec::fillLoginFields(shortSchema, QStringLiteral("p"), 1,
                                                                  std::nullopt, AnsiCodec::cp1252()),
                             UvException);
    // A field containing the wire delimiter would corrupt the packet.
    QStringList delimited = metadata;
    delimited[5] = QStringLiteral("a:b");
    QVERIFY_EXCEPTION_THROWN(AuthenticationCodec::fillLoginFields(delimited, QStringLiteral("p"), 1,
                                                                  std::nullopt, AnsiCodec::cp1252()),
                             UvException);
}

void TestAuth::connectionRequestCommand()
{
    const QJsonObject fixture = uvtest::Fixtures::instance().object(QStringLiteral("connection_request"));
    const QStringList templateTerms = jsonStrings(fixture.value(QStringLiteral("template_terms")).toArray());
    QCOMPARE(templateTerms.size(), 17);

    const QJsonArray cases = fixture.value(QStringLiteral("cases")).toArray();
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        ConnectionRequestContext context;
        context.templateTerms = jsonStrings(entry.value(QStringLiteral("terms")).toArray());
        context.password = entry.value(QStringLiteral("password")).toString();
        context.passwordType = entry.value(QStringLiteral("password_type")).toInt();
        context.codec = AnsiCodec::cp1252();
        context.registrationTerm2 = entry.value(QStringLiteral("registration_term2")).toString();
        const qint32 clientId = entry.value(QStringLiteral("client_id")).toInt();

        const QStringList terms = AuthenticationCodec::resolveConnectionRequestTerms(context, clientId);
        QCOMPARE(terms.size(), 17);
        QCOMPARE(terms.at(6), QString::number(clientId));
        QCOMPARE(terms.at(7), md5HexLower(context.codec.encode(terms.at(6) + context.registrationTerm2)));
        QCOMPARE(AuthenticationCodec::buildConnectionRequestCommand(terms, context.codec),
                 uvtest::Fixtures::entryBytes(entry));
    }
}

void TestAuth::connectionRequestValidation()
{
    QStringList terms = uvtest::testRequestTerms();
    QCOMPARE(terms.size(), 17);
    QVERIFY(!AuthenticationCodec::buildConnectionRequestCommand(terms, AnsiCodec::cp1252()).isEmpty());

    QStringList shortTerms = terms;
    shortTerms.removeLast();
    QVERIFY_EXCEPTION_THROWN(AuthenticationCodec::buildConnectionRequestCommand(shortTerms, AnsiCodec::cp1252()),
                             UvException);

    QStringList delimited = terms;
    delimited[3] = QStringLiteral("a:b");
    QVERIFY_EXCEPTION_THROWN(AuthenticationCodec::buildConnectionRequestCommand(delimited, AnsiCodec::cp1252()),
                             UvException);

    QStringList nullTerm = terms;
    nullTerm[3] = QString(QChar(0));
    QVERIFY_EXCEPTION_THROWN(AuthenticationCodec::buildConnectionRequestCommand(nullTerm, AnsiCodec::cp1252()),
                             UvException);

    ConnectionRequestContext context;
    context.templateTerms = shortTerms;
    QVERIFY_EXCEPTION_THROWN(AuthenticationCodec::resolveConnectionRequestTerms(context, 1), UvException);
}

void TestAuth::p2pSuffix()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("p2p_suffix"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        QCOMPARE(AuthenticationCodec::connectionRequestP2pSuffix(
                     entry.value(QStringLiteral("enabled")).toBool(),
                     entry.value(QStringLiteral("public_ip")).toString(),
                     entry.value(QStringLiteral("public_source_port")).toString(),
                     entry.value(QStringLiteral("local_ip")).toString(),
                     entry.value(QStringLiteral("local_port")).toString(),
                     entry.value(QStringLiteral("encryption_key")).toString()),
                 entry.value(QStringLiteral("value")).toString());
    }
    // A delimiter inside a field is refused instead of being escaped.
    QVERIFY_EXCEPTION_THROWN(AuthenticationCodec::connectionRequestP2pSuffix(
                                 true, QStringLiteral("1.2.3.4"), QStringLiteral("1|2"),
                                 QStringLiteral("10.0.0.5"), QStringLiteral("2"), QStringLiteral("K")),
                             UvException);
}

void TestAuth::p2pKeyDerivation()
{
    const QJsonObject p2p = uvtest::Fixtures::instance().object(QStringLiteral("p2p"));
    for (const QJsonValue &value : p2p.value(QStringLiteral("key_derivation")).toArray()) {
        const QJsonObject entry = value.toObject();
        QCOMPARE(P2pCodec::deriveAesKey(entry.value(QStringLiteral("password")).toString()),
                 uvtest::Fixtures::entryBytes(entry));
    }
    QVERIFY_EXCEPTION_THROWN(P2pCodec::deriveAesKey(QStringLiteral("p\u00e4ssword")), UvException);
    QVERIFY_EXCEPTION_THROWN(P2pCodec::deriveAesKey(QStringLiteral("\u4e2d")), UvException);
}

void TestAuth::p2pEncryption()
{
    const QJsonObject p2p = uvtest::Fixtures::instance().object(QStringLiteral("p2p"));
    for (const QJsonValue &value : p2p.value(QStringLiteral("encrypt")).toArray()) {
        const QJsonObject entry = value.toObject();
        const QString password = entry.value(QStringLiteral("password")).toString();
        const QByteArray iv = uvtest::Fixtures::entryBytes(entry, QStringLiteral("iv_hex"));
        const QByteArray plaintext = uvtest::Fixtures::entryBytes(entry, QStringLiteral("plaintext_hex"));
        const QByteArray body = uvtest::Fixtures::entryBytes(entry, QStringLiteral("body_hex"));
        QCOMPARE(P2pCodec::encryptBody(plaintext, password, iv), body);
        QCOMPARE(P2pCodec::decryptBody(body, password), plaintext);
    }
}

void TestAuth::p2pFrames()
{
    const QJsonObject p2p = uvtest::Fixtures::instance().object(QStringLiteral("p2p"));
    for (const QJsonValue &value : p2p.value(QStringLiteral("frames")).toArray()) {
        const QJsonObject entry = value.toObject();
        const QString password = entry.value(QStringLiteral("password")).toString();
        const bool unicodeBytes = entry.value(QStringLiteral("unicode_bytes")).toBool();
        const QByteArray payload = uvtest::Fixtures::entryBytes(entry, QStringLiteral("payload_hex"));
        const QByteArray wire = uvtest::Fixtures::entryBytes(entry, QStringLiteral("wire_hex"));
        const QByteArray iv = uvtest::Fixtures::entryBytes(entry, QStringLiteral("iv_hex"));
        QCOMPARE(P2pCodec::encodeFrame(payload, unicodeBytes, std::optional<QString>(password), iv), wire);

        const P2pCodec::DecodedBody decoded = P2pCodec::decodeBody(wire.mid(4), std::optional<QString>(password));
        QCOMPARE(decoded.payload, payload);
        QCOMPARE(decoded.unicodeBytes, unicodeBytes);
    }
    for (const QJsonValue &value : p2p.value(QStringLiteral("plain_frames")).toArray()) {
        const QJsonObject entry = value.toObject();
        const QByteArray payload = uvtest::Fixtures::entryBytes(entry, QStringLiteral("payload_hex"));
        const bool unicodeBytes = entry.value(QStringLiteral("unicode_bytes")).toBool();
        const QByteArray wire = uvtest::Fixtures::entryBytes(entry, QStringLiteral("wire_hex"));
        QCOMPARE(P2pCodec::encodeFrame(payload, unicodeBytes, std::nullopt), wire);
        const P2pCodec::DecodedBody decoded = P2pCodec::decodeBody(wire.mid(4), std::nullopt);
        QCOMPARE(decoded.payload, payload);
        QCOMPARE(decoded.unicodeBytes, unicodeBytes);
    }
    for (const QJsonValue &value : p2p.value(QStringLiteral("decrypt_body")).toArray()) {
        const QJsonObject entry = value.toObject();
        const P2pCodec::DecodedBody decoded =
            P2pCodec::decodeBody(uvtest::Fixtures::entryBytes(entry, QStringLiteral("body_hex")),
                                 std::optional<QString>(entry.value(QStringLiteral("password")).toString()));
        QCOMPARE(decoded.payload, uvtest::Fixtures::entryBytes(entry, QStringLiteral("payload_hex")));
        QCOMPARE(decoded.unicodeBytes, entry.value(QStringLiteral("flag")).toBool());
    }
    // Only an empty body is refused; any non-zero flag means "UTF-16LE payload",
    // so an unknown flag value is not an error (the reference tests `body[0] > 0`).
    QVERIFY_EXCEPTION_THROWN(P2pCodec::decodeBody(QByteArray(), std::nullopt), UvException);
    const P2pCodec::DecodedBody flagged = P2pCodec::decodeBody(QByteArrayLiteral("\x02x"), std::nullopt);
    QCOMPARE(flagged.unicodeBytes, true);
    QCOMPARE(flagged.payload, QByteArrayLiteral("x"));
    QVERIFY_EXCEPTION_THROWN(
        P2pCodec::encodeFrame(QByteArrayLiteral("x"), true, std::optional<QString>(QStringLiteral("p")),
                              QByteArray(4, '\0')),
        UvException);
}

void TestAuth::p2pTickEntry()
{
    // 0 <= delta <= 12'000'000 counts as "still inside the tick entry".
    QVERIFY(P2pCodec::disabledByTickEntry(0, 0));
    QVERIFY(P2pCodec::disabledByTickEntry(12000000, 0));
    QVERIFY(!P2pCodec::disabledByTickEntry(12000001, 0));
    // The recovered comparison is a signed subtraction, not wrap-safe elapsed time.
    QVERIFY(!P2pCodec::disabledByTickEntry(-2147483648, 2147483647));
}

void TestAuth::initialiserNoop()
{
    // Keeps the file honest about the shared helper namespace: the test data used
    // by every test is loaded once and must contain the documented keys.
    const QJsonObject root = uvtest::Fixtures::instance().root();
    for (const char *key : {"registration_command", "command3_payloads", "screen_sequences", "route",
                            "input_commands", "byte_array_fields", "frame_stream", "p2p"}) {
        QVERIFY2(root.contains(QLatin1String(key)), key);
    }
    QVERIFY(!uvtest::Fixtures::instance().dictionary().isEmpty());
    QVERIFY(uvtest::Fixtures::instance().dictionaryPath().endsWith(QStringLiteral("test-dict.bin")));
    QVERIFY(uvtest::Fixtures::instance().tlsCertificatePath().endsWith(QStringLiteral("server.crt")));
}

QTEST_MAIN(TestAuth)
#include "tst_auth.moc"
