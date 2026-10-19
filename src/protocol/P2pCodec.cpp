#include "protocol/P2pCodec.h"

#include "core/ErrorTypes.h"
#include "protocol/Digest.h"
#include "protocol/PacketCodec.h"

#include <QRandomGenerator>

#include <openssl/evp.h>

#include <array>
#include <cstring>
#include <memory>

namespace ultraview::protocol {
namespace {

struct CipherContextDeleter {
    void operator()(EVP_CIPHER_CTX *ctx) const { EVP_CIPHER_CTX_free(ctx); }
};
using CipherContextPtr = std::unique_ptr<EVP_CIPHER_CTX, CipherContextDeleter>;

struct CipherDeleter {
    void operator()(EVP_CIPHER *cipher) const { EVP_CIPHER_free(cipher); }
};
using CipherPtr = std::unique_ptr<EVP_CIPHER, CipherDeleter>;

const EVP_CIPHER *aes256Cbc()
{
    static CipherPtr cached{EVP_CIPHER_fetch(nullptr, "AES-256-CBC", nullptr)};
    if (!cached)
        throwError(ErrorCode::Internal, QStringLiteral("OpenSSL has no AES-256-CBC implementation"));
    return cached.get();
}

QByteArray aesCrypt(bool encrypt, const QByteArray &input, const QByteArray &key, const QByteArray &iv)
{
    if (input.size() > kAbsoluteMaxFrameBytes)
        throwResource(QStringLiteral("P2P crypto input exceeds limit"));
    CipherContextPtr ctx{EVP_CIPHER_CTX_new()};
    if (!ctx)
        throwError(ErrorCode::Internal, QStringLiteral("EVP_CIPHER_CTX_new failed"));
    const uchar *keyBytes = reinterpret_cast<const uchar *>(key.constData());
    const uchar *ivBytes = reinterpret_cast<const uchar *>(iv.constData());
    const bool initOk = encrypt
        ? EVP_EncryptInit_ex(ctx.get(), aes256Cbc(), nullptr, keyBytes, ivBytes) == 1
        : EVP_DecryptInit_ex(ctx.get(), aes256Cbc(), nullptr, keyBytes, ivBytes) == 1;
    if (!initOk)
        throwError(ErrorCode::Internal, QStringLiteral("AES init failed"));
    EVP_CIPHER_CTX_set_padding(ctx.get(), 1);

    const int blockSize = EVP_CIPHER_CTX_get_block_size(ctx.get());
    QByteArray out;
    out.resize(input.size() + blockSize);
    int produced = 0;
    uchar *outPtr = reinterpret_cast<uchar *>(out.data());
    const uchar *in = reinterpret_cast<const uchar *>(input.constData());
    const bool updateOk = encrypt
        ? EVP_EncryptUpdate(ctx.get(), outPtr, &produced, in, static_cast<int>(input.size())) == 1
        : EVP_DecryptUpdate(ctx.get(), outPtr, &produced, in, static_cast<int>(input.size())) == 1;
    if (!updateOk)
        throwProtocol(QStringLiteral("P2P cipher operation failed"));
    int finalised = 0;
    const bool finalOk = encrypt
        ? EVP_EncryptFinal_ex(ctx.get(), outPtr + produced, &finalised) == 1
        : EVP_DecryptFinal_ex(ctx.get(), outPtr + produced, &finalised) == 1;
    if (!finalOk) {
        if (encrypt)
            throwError(ErrorCode::Internal, QStringLiteral("P2P encryption finalisation failed"));
        throwProtocol(QStringLiteral("P2P ciphertext has invalid PKCS#7 padding"));
    }
    out.resize(produced + finalised);
    return out;
}

} // namespace

QByteArray P2pCodec::deriveAesKey(const QString &password)
{
    // ASCII-only subset: the recovered key alphabet is generated ASCII, and .NET's
    // replacement fallback for other input is intentionally not approximated.
    for (const QChar unit : password) {
        if (unit.unicode() > 0x7F)
            throwError(ErrorCode::Configuration, QStringLiteral("P2P password must be ASCII"));
    }
    const QByteArray ascii = password.toLatin1();
    // MD5 copied at offset 0 and again at offset 15 into a zeroed 32-byte key.
    const QByteArray md5 = QByteArray::fromHex(md5HexLower(ascii).toLatin1());
    QByteArray key;
    key.reserve(32);
    key.append(md5.left(15));
    key.append(md5);
    key.append('\0');
    return key;
}

QByteArray P2pCodec::encryptBody(const QByteArray &plaintext, const QString &password,
                                 std::optional<QByteArray> iv)
{
    QByteArray initialisationVector;
    if (iv.has_value()) {
        initialisationVector = *iv;
        if (initialisationVector.size() != 16)
            throwError(ErrorCode::Configuration, QStringLiteral("P2P IV must contain 16 bytes"));
    } else {
        std::array<quint32, 4> randomWords{};
        QRandomGenerator::system()->fillRange(randomWords.data(), randomWords.size());
        initialisationVector.resize(16);
        std::memcpy(initialisationVector.data(), randomWords.data(), 16);
    }
    return initialisationVector + aesCrypt(true, plaintext, deriveAesKey(password), initialisationVector);
}

QByteArray P2pCodec::decryptBody(const QByteArray &body, const QString &password)
{
    if (body.size() < 32 || (body.size() - 16) % 16 != 0)
        throwProtocol(QStringLiteral("P2P body needs IV16 and complete ciphertext blocks"));
    const QByteArray iv = body.left(16);
    return aesCrypt(false, body.mid(16), deriveAesKey(password), iv);
}

QByteArray P2pCodec::encodeFlaggedBody(const QByteArray &payload, bool unicodeBytes)
{
    QByteArray body;
    body.reserve(payload.size() + 1);
    body.append(static_cast<char>(unicodeBytes ? 1 : 0));
    body.append(payload);
    return body;
}

QByteArray P2pCodec::encodeFrame(const QByteArray &payload, bool unicodeBytes,
                                 const std::optional<QString> &encryptionPassword,
                                 std::optional<QByteArray> iv)
{
    QByteArray body = encodeFlaggedBody(payload, unicodeBytes);
    if (encryptionPassword.has_value()) {
        body = encryptBody(body, *encryptionPassword, iv);
    } else if (iv.has_value()) {
        throwError(ErrorCode::Configuration, QStringLiteral("an IV requires enabled P2P encryption"));
    }
    return encodeTransportFrame(body);
}

P2pCodec::DecodedBody P2pCodec::decodeBody(const QByteArray &body,
                                           const std::optional<QString> &encryptionPassword)
{
    QByteArray decoded = encryptionPassword.has_value() ? decryptBody(body, *encryptionPassword) : body;
    if (decoded.isEmpty())
        throwProtocol(QStringLiteral("P2P body requires an encoding flag"));
    DecodedBody result;
    result.unicodeBytes = static_cast<quint8>(decoded.at(0)) > 0;
    result.payload = decoded.mid(1);
    return result;
}

bool P2pCodec::disabledByTickEntry(qint32 nowSigned, qint32 savedSigned)
{
    const qint64 delta = qint64{nowSigned} - qint64{savedSigned};
    return delta >= 0 && delta <= 12000000;
}

} // namespace ultraview::protocol
