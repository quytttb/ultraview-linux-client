#include "protocol/ZstdCodec.h"

#include <QCoreApplication>
#include "core/ErrorTypes.h"

#include <zstd.h>

namespace ultraview::protocol {
namespace {

QString zstdErrorText(size_t code)
{
    return QString::fromUtf8(ZSTD_getErrorName(code));
}

} // namespace

bool ZstdCodec::hasFrameMagic(const QByteArray &payload)
{
    return payload.size() >= 4 && static_cast<quint8>(payload.at(0)) == 0x28
        && static_cast<quint8>(payload.at(1)) == 0xb5
        && static_cast<quint8>(payload.at(2)) == 0x2f
        && static_cast<quint8>(payload.at(3)) == 0xfd;
}

QByteArray ZstdCodec::compress(const QByteArray &payload, int level)
{
    const size_t bound = ZSTD_compressBound(static_cast<size_t>(payload.size()));
    QByteArray output(static_cast<qsizetype>(bound), Qt::Uninitialized);
    const size_t written = ZSTD_compress(output.data(), bound, payload.constData(),
                                        static_cast<size_t>(payload.size()), level);
    if (ZSTD_isError(written))
        return QByteArray();
    output.resize(static_cast<qsizetype>(written));
    return output;
}

QByteArray ZstdCodec::decompress(const QByteArray &payload, qint64 expectedSize, qint64 limit)
{
    if (expectedSize < 0)
        throwProtocol(QCoreApplication::translate("ZstdCodec", "peer message declares a negative length"));
    if (expectedSize > limit) {
        throwError(ErrorCode::Resource,
                   QCoreApplication::translate("ZstdCodec", "peer message is larger than this client accepts"),
                   QStringLiteral("%1 bytes requested, limit %2").arg(expectedSize).arg(limit));
    }
    // The frame must not claim more than the caller announced; checking the
    // header first avoids allocating for a decompression bomb.
    const unsigned long long declared = ZSTD_getFrameContentSize(payload.constData(),
                                                                static_cast<size_t>(payload.size()));
    if (declared != ZSTD_CONTENTSIZE_UNKNOWN && declared != ZSTD_CONTENTSIZE_ERROR
        && declared > static_cast<unsigned long long>(limit)) {
        throwError(ErrorCode::Resource,
                   QCoreApplication::translate("ZstdCodec", "compressed peer message declares an excessive size"),
                   QStringLiteral("%1 bytes, limit %2").arg(declared).arg(limit));
    }

    QByteArray output(static_cast<qsizetype>(expectedSize), Qt::Uninitialized);
    const size_t produced = ZSTD_decompress(output.data(), static_cast<size_t>(expectedSize),
                                            payload.constData(),
                                            static_cast<size_t>(payload.size()));
    if (ZSTD_isError(produced)) {
        throwProtocol(QCoreApplication::translate("ZstdCodec", "a compressed peer message could not be decompressed"),
                      zstdErrorText(produced));
    }
    if (produced != static_cast<size_t>(expectedSize)) {
        throwProtocol(QCoreApplication::translate("ZstdCodec", "a compressed peer message has an unexpected length"),
                      QStringLiteral("%1 bytes decoded, %2 announced").arg(produced).arg(expectedSize));
    }
    return output;
}

QByteArray ZstdCodec::decompressFrame(const QByteArray &payload, qint64 limit)
{
    const unsigned long long declared = ZSTD_getFrameContentSize(payload.constData(),
                                                                static_cast<size_t>(payload.size()));
    if (declared == ZSTD_CONTENTSIZE_ERROR)
        throwProtocol(QCoreApplication::translate("ZstdCodec", "a compressed peer message is not a Zstandard frame"));
    if (declared == ZSTD_CONTENTSIZE_UNKNOWN) {
        throwProtocol(QCoreApplication::translate("ZstdCodec", "a compressed peer message does not declare its size"),
                      QCoreApplication::translate("ZstdCodec", "the length must be carried by the message header"));
    }
    return decompress(payload, static_cast<qint64>(declared), limit);
}

} // namespace ultraview::protocol
