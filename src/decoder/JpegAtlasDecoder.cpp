#include "decoder/JpegAtlasDecoder.h"

#include "core/ErrorTypes.h"

#include <QImageReader>
#include <QBuffer>

namespace ultraview::decoder {
namespace {

qint16 readInt16(const char *data)
{
    const uchar *bytes = reinterpret_cast<const uchar *>(data);
    return static_cast<qint16>(static_cast<quint16>(bytes[0]) | (static_cast<quint16>(bytes[1]) << 8));
}

QImage flipVertical(const QImage &image)
{
    // `ImageOps.flip` == mirror rows about the horizontal axis.
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return image.flipped(Qt::Vertical);
#else
    return image.mirrored(false, true);
#endif
}

} // namespace

QImage JpegAtlasDecoder::loadBottomUpAtlas(const QByteArray &jpegBytes)
{
    if (jpegBytes.isEmpty())
        throwProtocol(QStringLiteral("empty JPEG atlas"));
    if (!jpegBytes.startsWith("\xff\xd8"))
        throwProtocol(QStringLiteral("invalid JPEG atlas magic"));

    QBuffer buffer;
    buffer.setData(jpegBytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "JPEG");
    // Inspect dimensions before decoding, not after an untrusted allocation.
    const QSize size = reader.size();
    if (size.width() <= 0 || size.height() <= 0)
        throwProtocol(QStringLiteral("JPEG atlas has invalid dimensions"));
    if (qint64{size.width()} * qint64{size.height()} > kMaxAtlasPixels) {
        throwResource(QStringLiteral("JPEG atlas outside the allocation policy"),
                      QStringLiteral("%1x%2").arg(size.width()).arg(size.height()));
    }
    const QImage source = reader.read();
    if (source.isNull())
        throwProtocol(QStringLiteral("JPEG atlas could not be decoded"));
    // Native TJFLAG_BOTTOMUP then ordinal 60 reads increasing rows.
    return flipVertical(source.convertToFormat(QImage::Format_RGB32));
}

QList<QRect> JpegAtlasDecoder::parseRectangles(const QByteArray &payload)
{
    if (payload.isEmpty() || payload.size() % 8 != 0)
        throwProtocol(QStringLiteral("invalid atlas rectangle record length"));
    QList<QRect> rectangles;
    rectangles.reserve(payload.size() / 8);
    for (int offset = 0; offset + 8 <= payload.size(); offset += 8) {
        const int x = readInt16(payload.constData() + offset);
        const int y = readInt16(payload.constData() + offset + 2);
        const int width = readInt16(payload.constData() + offset + 4);
        const int height = readInt16(payload.constData() + offset + 6);
        rectangles.append(QRect(x, y, width, height));
    }
    return rectangles;
}

QList<TilePlacement> JpegAtlasDecoder::layout(const QList<QRect> &rectangles, int blockSize, int atlasHeight)
{
    if (blockSize <= 0 || atlasHeight <= 0)
        throwError(ErrorCode::Configuration, QStringLiteral("invalid atlas geometry"));

    QList<TilePlacement> placements;
    int atlasY = 0;
    int column = 0;
    for (const QRect &rectangle : rectangles) {
        if (rectangle.width() < 0 || rectangle.height() < 0)
            throwProtocol(QStringLiteral("negative rectangle dimensions"));
        for (int dx = 0; dx < rectangle.width(); dx += blockSize) {
            const int tileWidth = qMin(blockSize, rectangle.width() - dx);
            for (int dy = 0; dy < rectangle.height(); dy += blockSize) {
                const int tileHeight = qMin(blockSize, rectangle.height() - dy);
                if (tileHeight > atlasHeight)
                    throwProtocol(QStringLiteral("tile cannot fit atlas height"));
                if (atlasY + tileHeight > atlasHeight) {
                    atlasY = 0;
                    ++column;
                }
                placements.append(TilePlacement{rectangle.x() + dx, rectangle.y() + dy,
                                                tileWidth, tileHeight,
                                                column * blockSize, atlasY});
                atlasY += tileHeight;
            }
        }
    }
    return placements;
}

} // namespace ultraview::decoder
