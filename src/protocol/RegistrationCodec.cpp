#include "protocol/RegistrationCodec.h"

#include <QCoreApplication>
#include "core/ErrorTypes.h"
#include "protocol/Digest.h"
#include "protocol/PacketCodec.h"

#include <QString>
#include <QStringList>

#include <openssl/evp.h>

#include <array>
#include <memory>

namespace ultraview::protocol {
namespace {

/// RAII wrapper for EVP_CIPHER_CTX.  OpenSSL 3 keeps DES-EDE3-CBC in the default
/// provider, so no legacy provider is needed; the cipher is still fetched by name
/// so a missing implementation fails loudly instead of silently.
struct CipherContextDeleter {
    void operator()(EVP_CIPHER_CTX *ctx) const { EVP_CIPHER_CTX_free(ctx); }
};
using CipherContextPtr = std::unique_ptr<EVP_CIPHER_CTX, CipherContextDeleter>;

struct CipherDeleter {
    void operator()(EVP_CIPHER *cipher) const { EVP_CIPHER_free(cipher); }
};
using CipherPtr = std::unique_ptr<EVP_CIPHER, CipherDeleter>;

const EVP_CIPHER *tripleDesCbc()
{
    static CipherPtr cached{[] {
        EVP_CIPHER *cipher = EVP_CIPHER_fetch(nullptr, "DES-EDE3-CBC", nullptr);
        return cipher;
    }()};
    if (!cached)
        throwError(ErrorCode::Internal,
                   QCoreApplication::translate("RegistrationCodec", "OpenSSL has no DES-EDE3-CBC implementation available"));
    return cached.get();
}

QByteArray crypt(bool encrypt, const QByteArray &input, const QByteArray &key, const QByteArray &iv)
{
    if (key.size() != 24)
        throwError(ErrorCode::Configuration, QCoreApplication::translate("RegistrationCodec", "registration key must be 24 bytes"));
    if (iv.size() != 8)
        throwError(ErrorCode::Configuration, QCoreApplication::translate("RegistrationCodec", "registration IV must be 8 bytes"));
    if (input.size() > kAbsoluteMaxFrameBytes)
        throwResource(QCoreApplication::translate("RegistrationCodec", "registration crypto input exceeds limit"));
    // An empty input is valid: PKCS#7 turns it into one full block of padding,
    // exactly as .NET's TripleDES does, so the reference fixture pins the
    // empty-plaintext ciphertext to eight bytes.

    CipherContextPtr ctx{EVP_CIPHER_CTX_new()};
    if (!ctx)
        throwError(ErrorCode::Internal, QCoreApplication::translate("RegistrationCodec", "EVP_CIPHER_CTX_new failed"));

    const EVP_CIPHER *cipher = tripleDesCbc();
    const uchar *keyBytes = reinterpret_cast<const uchar *>(key.constData());
    const uchar *ivBytes = reinterpret_cast<const uchar *>(iv.constData());
    const bool ok = encrypt
        ? EVP_EncryptInit_ex(ctx.get(), cipher, nullptr, keyBytes, ivBytes) == 1
        : EVP_DecryptInit_ex(ctx.get(), cipher, nullptr, keyBytes, ivBytes) == 1;
    if (!ok)
        throwError(ErrorCode::Internal, QCoreApplication::translate("RegistrationCodec", "cipher init failed"));
    // .NET Framework TripleDES uses PKCS#7 padding by default; OpenSSL's default
    // padding is PKCS#7 as well, so padding is intentionally left enabled.
    EVP_CIPHER_CTX_set_padding(ctx.get(), 1);

    const int blockSize = EVP_CIPHER_CTX_get_block_size(ctx.get());
    QByteArray out;
    out.resize(input.size() + blockSize);
    int produced = 0;
    const uchar *in = reinterpret_cast<const uchar *>(input.constData());
    uchar *outPtr = reinterpret_cast<uchar *>(out.data());
    const bool updateOk = encrypt
        ? EVP_EncryptUpdate(ctx.get(), outPtr, &produced, in, static_cast<int>(input.size())) == 1
        : EVP_DecryptUpdate(ctx.get(), outPtr, &produced, in, static_cast<int>(input.size())) == 1;
    if (!updateOk)
        throwProtocol(QCoreApplication::translate("RegistrationCodec", "registration cipher operation failed"));
    int finalised = 0;
    const bool finalOk = encrypt
        ? EVP_EncryptFinal_ex(ctx.get(), outPtr + produced, &finalised) == 1
        : EVP_DecryptFinal_ex(ctx.get(), outPtr + produced, &finalised) == 1;
    if (!finalOk) {
        if (encrypt)
            throwError(ErrorCode::Internal, QCoreApplication::translate("RegistrationCodec", "registration encryption finalisation failed"));
        throwProtocol(QCoreApplication::translate("RegistrationCodec", "registration ciphertext has invalid PKCS#7 padding"));
    }
    out.resize(produced + finalised);
    return out;
}

bool isAsciiDigits(const QString &value)
{
    if (value.isEmpty())
        return false;
    for (const QChar unit : value) {
        if (unit < QLatin1Char('0') || unit > QLatin1Char('9'))
            return false;
    }
    return true;
}

} // namespace

QByteArray registrationPassword()
{
    static const std::array<char, 5> kCharacters{5, 10, 15, 20, 25};
    return QByteArray(kCharacters.data(), static_cast<int>(kCharacters.size()));
}

QByteArray RegistrationCodec::deriveVFunctionKeyBytes(const QString &password, int length)
{
    if (length < 0)
        throwError(ErrorCode::Configuration, QCoreApplication::translate("RegistrationCodec", "negative key length"));
    const QByteArray digest = sha1Bytes(utf16leBytes(password));
    QByteArray out = digest;
    if (out.size() < length)
        out.append(QByteArray(length - out.size(), '\0'));
    out.truncate(length);
    return out;
}

QByteArray RegistrationCodec::encryptRegistrationBytes(const QByteArray &payload, const QByteArray &password)
{
    return crypt(true, payload,
                 deriveVFunctionKeyBytes(QString::fromLatin1(password), 24),
                 deriveVFunctionKeyBytes(QString(), 8));
}

QByteArray RegistrationCodec::decryptRegistrationBytes(const QByteArray &ciphertext, const QByteArray &password)
{
    return crypt(false, ciphertext,
                 deriveVFunctionKeyBytes(QString::fromLatin1(password), 24),
                 deriveVFunctionKeyBytes(QString(), 8));
}

QString RegistrationCodec::fingerprintDigest(const QByteArray &preimage)
{
    return md5HexUpper(preimage);
}

QString RegistrationCodec::fallbackFingerprint(const QString &macAddress,
                                               const QString &programFilesCreationTicks,
                                               const QString &systemDirectoryCreationTicks,
                                               const AnsiCodec &codec)
{
    // Concatenation has no separator (observed at 63c66b..63c7a5).
    const QString value = macAddress + programFilesCreationTicks + systemDirectoryCreationTicks;
    return fingerprintDigest(codec.encode(value));
}

QString RegistrationCodec::validateRegistrationTerms(const QStringList &terms)
{
    if (terms.size() != 26)
        return QCoreApplication::translate("RegistrationCodec", "registration requires exactly 26 terms (got %1)").arg(terms.size());
    const auto expect = [&terms](int index, const char *value) -> QString {
        if (terms.at(index) != QLatin1String(value))
            return QCoreApplication::translate("RegistrationCodec", "term %1 must equal \"%2\"").arg(index).arg(QLatin1String(value));
        return {};
    };
    for (const auto &pair : {std::pair<int, const char *>{0, "3"}, {3, "0"}, {5, "0"}, {12, ""}}) {
        if (const QString reason = expect(pair.first, pair.second); !reason.isEmpty())
            return reason;
    }
    for (int index = 0; index < terms.size(); ++index) {
        const QString &value = terms.at(index);
        if (value.contains(QChar(u'\0')))
            return QCoreApplication::translate("RegistrationCodec", "term %1 contains a NUL character").arg(index);
        if (index != 22 && value.contains(QLatin1Char(':')))
            return QCoreApplication::translate("RegistrationCodec", "term %1 contains ':'").arg(index);
    }
    const QStringList ticks = terms.at(22).split(QLatin1Char(':'));
    if (ticks.size() != 3)
        return QCoreApplication::translate("RegistrationCodec", "term 22 requires three ':' separated tick strings");
    for (const QString &tick : ticks) {
        if (!isAsciiDigits(tick))
            return QCoreApplication::translate("RegistrationCodec", "term 22 tick \"%1\" must be unsigned decimal ASCII digits").arg(tick);
    }
    return {};
}

QByteArray RegistrationCodec::buildRegistrationCommand(const QStringList &terms, const AnsiCodec &codec)
{
    const QString reason = validateRegistrationTerms(terms);
    if (!reason.isEmpty())
        throwError(ErrorCode::Configuration, QCoreApplication::translate("RegistrationCodec", "invalid registration terms"), reason);
    // Strict encoding: never silently replace host metadata characters.
    return codec.encode(terms.join(QLatin1Char(':')));
}

Command3Identity RegistrationCodec::parseCommand3Identity(const QByteArray &payload, const AnsiCodec &codec)
{
    if (payload.isEmpty())
        throwProtocol(QCoreApplication::translate("RegistrationCodec", "empty command 3 reply"));
    const QStringList fields = codec.decode(payload).split(QLatin1Char(':'));
    if (fields.size() < 4 || fields.at(0) != QLatin1String("3"))
        throwProtocol(QCoreApplication::translate("RegistrationCodec", "expected command 3 with endpoint and ID fields"));

    const QString &portText = fields.at(1);
    const QString &idText = fields.at(2);
    for (const QString *value : {&portText, &idText}) {
        if (value->isEmpty() || !isAsciiDigits(*value)) {
            throwProtocol(QCoreApplication::translate("RegistrationCodec", "expected ASCII decimal numeric fields"),
                          value->isEmpty() ? QCoreApplication::translate("RegistrationCodec", "empty field") : *value);
        }
    }
    if (portText.size() > 5 || idText.size() > 10)
        throwProtocol(QCoreApplication::translate("RegistrationCodec", "numeric field exceeds modeled range"));

    bool portOk = false;
    bool idOk = false;
    const qint64 portNumber = portText.toLongLong(&portOk);
    const qint64 idNumber = idText.toLongLong(&idOk);
    if (!portOk || !idOk || portNumber < 0 || portNumber > 65535 || idNumber < 1 || idNumber > 2147483647)
        throwProtocol(QCoreApplication::translate("RegistrationCodec", "numeric field outside modeled range"));

    Command3Identity identity;
    identity.clientId = static_cast<qint32>(idNumber);
    identity.publicSourcePort = static_cast<quint16>(portNumber);
    identity.publicIp = fields.at(3);
    return identity;
}

qint32 RegistrationCodec::readReferralInt32(const QByteArray &data)
{
    if (data.size() < 4)
        throwProtocol(QCoreApplication::translate("RegistrationCodec", "Referral.ini requires at least four bytes"));
    const uchar *bytes = reinterpret_cast<const uchar *>(data.constData());
    const quint32 value = static_cast<quint32>(bytes[0]) | (static_cast<quint32>(bytes[1]) << 8)
        | (static_cast<quint32>(bytes[2]) << 16) | (static_cast<quint32>(bytes[3]) << 24);
    return static_cast<qint32>(value);
}

QString RegistrationCodec::formatOsVersionCombine(qint32 major, qint32 minor, qint32 build, qint32 revision)
{
    for (const qint32 value : {major, minor, build, revision}) {
        if (value < -1)
            throwError(ErrorCode::Configuration, QCoreApplication::translate("RegistrationCodec", "expected .NET Version component range"));
    }
    if (major < 0 || minor < 0)
        throwError(ErrorCode::Configuration, QCoreApplication::translate("RegistrationCodec", "major/minor must be nonnegative"));

    const auto signed16 = [](qint32 value) -> qint32 {
        const quint32 masked = static_cast<quint32>(value) & 0xFFFFu;
        return masked >= 0x8000u ? static_cast<qint32>(masked) - 0x10000 : static_cast<qint32>(masked);
    };
    const qint32 majorRevision = signed16(revision >> 16);
    const qint32 minorRevision = signed16(revision);
    return QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(build)
        .arg(major)
        .arg(majorRevision)
        .arg(minor)
        .arg(minorRevision)
        .arg(revision);
}

bool RegistrationCodec::versionEqualOrGreater(const QString &current, const QString &minimum)
{
    const QStringList left = current.split(QLatin1Char('.'));
    if (left.size() < 3)
        return false;
    const QStringList right = minimum.split(QLatin1Char('.'));
    if (right.size() < 3)
        throwError(ErrorCode::Configuration, QCoreApplication::translate("RegistrationCodec", "minimum version requires three components"));

    const auto component = [](const QString &value) -> quint64 {
        if (!isAsciiDigits(value))
            throwProtocol(QCoreApplication::translate("RegistrationCodec", "version component requires ASCII decimal digits"),
                          value.isEmpty() ? QCoreApplication::translate("RegistrationCodec", "empty component") : value);
        quint64 number = 0;
        for (const QChar unit : value) {
            const quint64 digit = static_cast<quint64>(unit.unicode() - u'0');
            if (number > (0x7FFFFFFFFFFFFFFFULL - digit) / 10ULL)
                throwProtocol(QCoreApplication::translate("RegistrationCodec", "version component exceeds signed Int64"));
            number = number * 10ULL + digit;
        }
        return number;
    };

    for (int index = 0; index < 3; ++index) {
        const quint64 a = component(left.at(index));
        const quint64 b = component(right.at(index));
        if (a != b)
            return a > b;
    }
    return true;
}

} // namespace ultraview::protocol
