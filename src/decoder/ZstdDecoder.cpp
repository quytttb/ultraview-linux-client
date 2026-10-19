#include "decoder/ZstdDecoder.h"

#include "core/ErrorTypes.h"

#include <zstd.h>

namespace ultraview::decoder {
namespace {

QString zstdErrorText(size_t code)
{
    return QString::fromUtf8(ZSTD_getErrorName(code));
}

} // namespace

ZstdDecoder::ZstdDecoder()
    : m_context(ZSTD_createDCtx())
{
    if (!m_context)
        throwError(ErrorCode::Resource, QStringLiteral("cannot create a Zstandard decompression context"));
}

ZstdDecoder::~ZstdDecoder()
{
    if (m_context)
        ZSTD_freeDCtx(m_context);
}

void ZstdDecoder::setDictionary(const QByteArray &dictionary)
{
    m_dictionary = dictionary;
    m_dictionaryLoaded = false;
    // Loading the dictionary once per context avoids re-parsing it for every
    // rectangle group of every frame.
    const size_t result = ZSTD_DCtx_loadDictionary(m_context, m_dictionary.constData(),
                                                   static_cast<size_t>(m_dictionary.size()));
    if (ZSTD_isError(result)) {
        throwError(ErrorCode::Configuration, QStringLiteral("Zstandard rejected the dictionary"),
                   zstdErrorText(result));
    }
    m_dictionaryLoaded = !m_dictionary.isEmpty();
}

void ZstdDecoder::setMaxOutputBytes(qint64 limit)
{
    if (limit <= 0 || limit > kDefaultMaxOutputBytes * 16)
        throwError(ErrorCode::Configuration, QStringLiteral("invalid Zstandard output limit"));
    m_maxOutputBytes = limit;
}

bool ZstdDecoder::hasFrameMagic(const QByteArray &payload)
{
    static const QByteArray magic = QByteArray::fromHex("28b52ffd");
    return payload.startsWith(magic);
}

qint64 ZstdDecoder::declaredContentSize(const QByteArray &payload)
{
    const unsigned long long size = ZSTD_getFrameContentSize(payload.constData(),
                                                             static_cast<size_t>(payload.size()));
    if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN)
        return -1;
    if (size > static_cast<unsigned long long>(std::numeric_limits<qint64>::max()))
        return -1;
    return static_cast<qint64>(size);
}

QByteArray ZstdDecoder::decompress(const QByteArray &payload, qint64 expectedSize)
{
    if (expectedSize <= 0)
        throwProtocol(QStringLiteral("the screen update declares no pixel bytes"));
    if (expectedSize > m_maxOutputBytes) {
        throwResource(QStringLiteral("decoded image exceeds the allocation policy"),
                      QStringLiteral("%1 > %2 bytes").arg(expectedSize).arg(m_maxOutputBytes));
    }
    if (payload.isEmpty())
        throwProtocol(QStringLiteral("empty Zstandard payload"));
    if (!hasFrameMagic(payload))
        throwProtocol(QStringLiteral("screen codec is outside the supported Zstandard subset"));

    // Refuse a bomb before allocating: the frame header is cheap to inspect.
    const qint64 declared = declaredContentSize(payload);
    if (declared > m_maxOutputBytes) {
        throwResource(QStringLiteral("Zstandard frame declares a size above the allocation policy"),
                      QStringLiteral("%1 > %2 bytes").arg(declared).arg(m_maxOutputBytes));
    }

    QByteArray output;
    // QByteArray would zero-fill too; resizing is the only way to get a writable
    // buffer of exactly the expected size through the Qt API.
    output.resize(static_cast<int>(expectedSize));

    if (m_dictionaryLoaded) {
        // The dictionary is already attached to the context.
    } else if (!m_dictionary.isEmpty()) {
        setDictionary(m_dictionary);
    }

    const size_t produced = ZSTD_decompressDCtx(m_context, output.data(),
                                                static_cast<size_t>(output.size()),
                                                payload.constData(),
                                                static_cast<size_t>(payload.size()));
    if (ZSTD_isError(produced)) {
        throwProtocol(QStringLiteral("Zstandard decoding failed"), zstdErrorText(produced));
    }
    if (static_cast<qint64>(produced) != expectedSize) {
        throwProtocol(QStringLiteral("the decoded image size differs from the declared rectangles"),
                      QStringLiteral("%1 != %2 bytes").arg(produced).arg(expectedSize));
    }
    return output;
}

} // namespace ultraview::decoder
