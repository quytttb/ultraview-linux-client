#include "decoder/ScreenDecoder.h"

#include "core/ErrorTypes.h"
#include "protocol/PacketCodec.h"

#include <QList>
#include <QSet>

#include <cstring>
#include <optional>

namespace ultraview::decoder {
namespace {

qint16 readInt16(const char *data)
{
    const uchar *bytes = reinterpret_cast<const uchar *>(data);
    return static_cast<qint16>(static_cast<quint16>(bytes[0]) | (static_cast<quint16>(bytes[1]) << 8));
}

quint16 readUInt16(const char *data)
{
    const uchar *bytes = reinterpret_cast<const uchar *>(data);
    return static_cast<quint16>(static_cast<quint16>(bytes[0]) | (static_cast<quint16>(bytes[1]) << 8));
}

qint32 readInt32(const char *data)
{
    const uchar *bytes = reinterpret_cast<const uchar *>(data);
    const quint32 value = static_cast<quint32>(bytes[0]) | (static_cast<quint32>(bytes[1]) << 8)
        | (static_cast<quint32>(bytes[2]) << 16) | (static_cast<quint32>(bytes[3]) << 24);
    return static_cast<qint32>(value);
}

/// Pillow BGR;15 scales by floor(component * 255 / 31), not bit replication.
/// The component-ramp fixture covers every value (e.g. 4 -> 32, not 33).
inline quint8 expand5(quint16 value)
{
    return static_cast<quint8>((value * 255) / 31);
}

/// Packed little-endian RGB555 into a premultiplication-free 32-bit RGB row.
inline QRgb rgb555ToRgb(quint16 value)
{
    const quint8 red = expand5(static_cast<quint16>((value >> 10) & 0x1F));
    const quint8 green = expand5(static_cast<quint16>((value >> 5) & 0x1F));
    const quint8 blue = expand5(static_cast<quint16>(value & 0x1F));
    return qRgb(red, green, blue);
}

QImage rgb555RectToImage(const QByteArray &stream, qint64 offset, int width, int height)
{
    // Row order pinned by the reference fixture: the first stream row is the
    // first (top) logical row. Pillow's raw orientation=1 is top-down;
    // orientation=-1, which the reference does not request, would reverse it.
    QImage image(width, height, QImage::Format_RGB32);
    if (image.isNull())
        throwResource(QStringLiteral("cannot allocate a screen rectangle"),
                      QStringLiteral("%1x%2").arg(width).arg(height));
    const char *input = stream.constData() + offset;
    for (int row = 0; row < height; ++row) {
        auto *target = reinterpret_cast<QRgb *>(image.scanLine(row));
        const char *sourceRow = input + qint64{row} * width * 2;
        for (int column = 0; column < width; ++column)
            target[column] = rgb555ToRgb(readUInt16(sourceRow + column * 2));
    }
    return image;
}

} // namespace

ScreenDecoder::ScreenDecoder()
{
    m_tileCache.setLimit(m_options.cacheLimit);
    m_zstd.setMaxOutputBytes(m_options.maxOutputBytes);
}

void ScreenDecoder::setOptions(const Options &options)
{
    if (options.blockSize <= 0)
        throwError(ErrorCode::Configuration, QStringLiteral("JPEG block size must be positive"));
    if (options.cacheLimit < 1)
        throwError(ErrorCode::Configuration, QStringLiteral("cache limit must be positive"));
    m_options = options;
    m_tileCache.setLimit(options.cacheLimit);
    m_zstd.setMaxOutputBytes(options.maxOutputBytes);
}

void ScreenDecoder::setDictionary(const QByteArray &dictionary)
{
    m_zstd.setDictionary(dictionary);
}

void ScreenDecoder::resetCanvas(const QSize &size)
{
    m_frameBuffer.reset(size);
}

QSize ScreenDecoder::desktopSizeFromLogin(const QList<QByteArray> &fields)
{
    if (fields.size() < 3 || fields.at(0).isEmpty()
        || static_cast<quint8>(fields.at(0).at(0)) != 0
        || fields.at(1).size() != 4 || fields.at(2).size() != 4) {
        throwProtocol(QStringLiteral("invalid login desktop dimensions"));
    }
    const qint32 width = readInt32(fields.at(1).constData());
    const qint32 height = readInt32(fields.at(2).constData());
    if (width <= 0 || height <= 0 || width > FrameBuffer::kMaxCanvasEdge
        || height > FrameBuffer::kMaxCanvasEdge
        || qint64{width} * qint64{height} > FrameBuffer::kMaxCanvasPixels) {
        throwProtocol(QStringLiteral("partner desktop dimensions outside allocation limit"),
                      QStringLiteral("%1x%2").arg(width).arg(height));
    }
    return QSize(width, height);
}

void ScreenDecoder::validate(const QList<QByteArray> &fields, Plan *plan)
{
    if (fields.size() != 11)
        throwProtocol(QStringLiteral("expected eleven-field kind1 update"),
                      QStringLiteral("got %1 fields").arg(fields.size()));
    if (fields.at(0).isEmpty() || static_cast<quint8>(fields.at(0).at(0)) != 1)
        throwProtocol(QStringLiteral("expected kind1 screen update"));
    if (!m_frameBuffer.isValid())
        throwProtocol(QStringLiteral("screen update arrived before the partner desktop size was known"));

    // BitBlt and every other unimplemented image field must be refused loudly.
    if (!fields.at(5).isEmpty()) {
        throwUnsupported(QStringLiteral("BitBlt screen operations are not supported yet"),
                         QStringLiteral("field 5 carries %1 bytes").arg(fields.at(5).size()));
    }
    if (fields.at(9).size() % 8 != 0)
        throwProtocol(QStringLiteral("invalid screen cache reference record length"));
    if (fields.at(10).size() % 12 != 0)
        throwProtocol(QStringLiteral("invalid screen cache addition record length"));

    const QSize canvasSize = m_frameBuffer.size();
    const auto bounds = [&canvasSize](int x, int y, int width, int height, const char *what) {
        if (width <= 0 || height <= 0 || x < 0 || y < 0 || x + width > canvasSize.width()
            || y + height > canvasSize.height()) {
            throwProtocol(QStringLiteral("%1 rectangle outside canvas").arg(QLatin1String(what)),
                          QStringLiteral("(%1,%2 %3x%4) on %5x%6")
                              .arg(x).arg(y).arg(width).arg(height)
                              .arg(canvasSize.width()).arg(canvasSize.height()));
        }
    };

    // --- field 6: scroll copy (reads the pre-update canvas) ----------------
    if (!fields.at(6).isEmpty()) {
        const QByteArray &payload = fields.at(6);
        if (payload.size() < 10 || (payload.size() - 10) % 8 != 0)
            throwProtocol(QStringLiteral("invalid scroll-copy record length"));
        ScrollCopy scroll;
        scroll.delta = readInt16(payload.constData());
        scroll.x = readInt16(payload.constData() + 2);
        scroll.y = readInt16(payload.constData() + 4);
        scroll.width = readInt16(payload.constData() + 6);
        scroll.height = readInt16(payload.constData() + 8);
        bounds(scroll.x, scroll.y, scroll.width, scroll.height, "scroll-copy");
        for (int offset = 10; offset < payload.size(); offset += 8) {
            const QRect rectangle(readInt16(payload.constData() + offset),
                                  readInt16(payload.constData() + offset + 2),
                                  readInt16(payload.constData() + offset + 4),
                                  readInt16(payload.constData() + offset + 6));
            bounds(rectangle.x(), rectangle.y(), rectangle.width(), rectangle.height(), "scroll sub-copy");
            // Reference: the sub-rectangle must stay inside the shifted region.
            if (rectangle.x() < scroll.x || rectangle.x() + rectangle.width() > scroll.x + scroll.width
                || rectangle.y() < scroll.y - scroll.delta
                || rectangle.y() + rectangle.height() > scroll.y - scroll.delta + scroll.height) {
                throwProtocol(QStringLiteral("scroll-copy references uncopied intermediate pixels"));
            }
            scroll.subRectangles.append(rectangle);
        }
        plan->scroll = scroll;
    }

    // --- field 9: cache references ----------------------------------------
    {
        const QByteArray &payload = fields.at(9);
        for (int offset = 0; offset + 8 <= payload.size(); offset += 8) {
            CacheReference reference;
            reference.x = readInt16(payload.constData() + offset);
            reference.y = readInt16(payload.constData() + offset + 2);
            reference.key = readInt32(payload.constData() + offset + 4);
            if (!m_tileCache.contains(reference.key))
                throwProtocol(QStringLiteral("screen cache reference is missing"),
                              QStringLiteral("key %1").arg(reference.key));
            const CachedTile *tile = m_tileCache.peek(reference.key);
            if (!tile)
                throwProtocol(QStringLiteral("screen cache reference is missing"));
            bounds(reference.x, reference.y, tile->image.width(), tile->image.height(), "cache reference");
            plan->references.append(reference);
        }
    }

    // --- fields 7/8: JPEG atlas -------------------------------------------
    if (!fields.at(7).isEmpty() || !fields.at(8).isEmpty()) {
        if (fields.at(7).isEmpty() || fields.at(8).isEmpty() || fields.at(8).size() % 8 != 0)
            throwProtocol(QStringLiteral("invalid JPEG atlas fields"));
        if (!fields.at(7).startsWith("\xff\xd8"))
            throwProtocol(QStringLiteral("invalid JPEG atlas magic"));
        plan->atlas = JpegAtlasDecoder::loadBottomUpAtlas(fields.at(7));
        const QList<QRect> rectangles = JpegAtlasDecoder::parseRectangles(fields.at(8));
        for (const QRect &rectangle : rectangles)
            bounds(rectangle.x(), rectangle.y(), rectangle.width(), rectangle.height(), "atlas");
        plan->atlasTiles = JpegAtlasDecoder::layout(rectangles, m_options.blockSize,
                                                    plan->atlas.height());
        for (const TilePlacement &placement : plan->atlasTiles) {
            if (placement.atlasX + placement.width > plan->atlas.width()
                || placement.atlasY + placement.height > plan->atlas.height()) {
                throwProtocol(QStringLiteral("JPEG tile outside atlas"),
                              QStringLiteral("atlas %1x%2 tile (%3,%4 %5x%6)")
                                  .arg(plan->atlas.width()).arg(plan->atlas.height())
                                  .arg(placement.atlasX).arg(placement.atlasY)
                                  .arg(placement.width).arg(placement.height));
            }
        }
    }

    // --- fields 1/2: RGB555 rectangle stream ------------------------------
    if (!fields.at(1).isEmpty() && !fields.at(2).isEmpty()) {
        const QByteArray &rectangleRecords = fields.at(1);
        if (rectangleRecords.size() % 8 != 0)
            throwProtocol(QStringLiteral("expected UInt16 x/y/w/h rectangle records"));
        qint64 expected = 0;
        for (int offset = 0; offset < rectangleRecords.size(); offset += 8) {
            PixelRect rectangle;
            rectangle.x = readUInt16(rectangleRecords.constData() + offset);
            rectangle.y = readUInt16(rectangleRecords.constData() + offset + 2);
            rectangle.width = readUInt16(rectangleRecords.constData() + offset + 4);
            rectangle.height = readUInt16(rectangleRecords.constData() + offset + 6);
            bounds(rectangle.x, rectangle.y, rectangle.width, rectangle.height, "pixel");
            // Checked arithmetic before allocating.
            const qint64 pixels = qint64{rectangle.width} * qint64{rectangle.height};
            if (pixels > (std::numeric_limits<qint64>::max() / 2) - expected)
                throwResource(QStringLiteral("screen rectangle pixel count overflows"));
            expected += pixels * 2;
            plan->pixelRectangles.append(rectangle);
        }
        plan->pixelData = m_zstd.decompress(fields.at(2), expected);
    } else if (!fields.at(1).isEmpty() || !fields.at(2).isEmpty()) {
        // Reference behaviour: `if fields[1] and fields[2]` -- one without the
        // other is skipped entirely.  Counted, never silently dropped undocumented.
        m_counters.skippedPixelPath += 1;
    }

    // --- field 10: cache additions ----------------------------------------
    {
        const QByteArray &payload = fields.at(10);
        qint64 projectedBytes = m_tileCache.bytes();
        QSet<qint32> newKeys;
        for (int offset = 0; offset + 12 <= payload.size(); offset += 12) {
            CacheAddition addition;
            addition.x = readInt16(payload.constData() + offset);
            addition.y = readInt16(payload.constData() + offset + 2);
            addition.width = readInt16(payload.constData() + offset + 4);
            addition.height = readInt16(payload.constData() + offset + 6);
            addition.key = readInt32(payload.constData() + offset + 8);
            bounds(addition.x, addition.y, addition.width, addition.height, "cache addition");
            if (!m_tileCache.contains(addition.key) && !newKeys.contains(addition.key)) {
                projectedBytes += qint64{addition.width} * addition.height * 4;
                if (projectedBytes > TileCache::kMaxCacheBytes)
                    throwResource(QStringLiteral("tile cache exceeds memory budget"));
                newKeys.insert(addition.key);
            }
            plan->additions.append(addition);
        }
    }
}

int ScreenDecoder::applyPlan(const Plan &plan)
{
    QImage &canvas = m_frameBuffer.canvas();
    int applied = 0;

    // 1. scroll copy: the recovered worker copies the source rectangle to
    //    (x, y - delta) inside a black intermediate bitmap and then pastes back
    //    only the listed sub-rectangles.  Validation guarantees every sub-rectangle
    //    lies inside the copied region, so the result for a sub-rectangle at
    //    (rx, ry) is the pre-update pixel at (rx, ry + delta); pixels outside the
    //    sub-rectangles keep their previous value (they are never pasted back).
    if (plan.scroll.has_value()) {
        const ScrollCopy &scroll = *plan.scroll;
        const QImage original = canvas;
        for (const QRect &rectangle : scroll.subRectangles) {
            for (int row = 0; row < rectangle.height(); ++row) {
                const int destinationRow = rectangle.y() + row;
                if (destinationRow < 0 || destinationRow >= canvas.height())
                    continue;
                const int sourceRow = destinationRow + scroll.delta;
                if (sourceRow < 0 || sourceRow >= original.height())
                    continue;
                int columns = rectangle.width();
                int column = rectangle.x();
                if (column < 0) {
                    columns += column;
                    column = 0;
                }
                if (column + columns > canvas.width())
                    columns = canvas.width() - column;
                if (columns <= 0)
                    continue;
                std::memcpy(canvas.scanLine(destinationRow) + qint64{column} * 4,
                            original.constScanLine(sourceRow) + qint64{column} * 4,
                            static_cast<size_t>(columns) * 4);
            }
            m_frameBuffer.markDirty(rectangle);
            ++applied;
        }
        m_counters.scrollCopies += 1;
    }

    // 2. cache references (this also updates the LRU order).
    for (const CacheReference &reference : plan.references) {
        const CachedTile *tile = m_tileCache.reference(reference.key);
        if (!tile)
            throwProtocol(QStringLiteral("screen cache reference is missing"));
        blit(tile->image, QRect(QPoint(0, 0), tile->image.size()), QPoint(reference.x, reference.y), false);
        m_frameBuffer.markDirty(QRect(reference.x, reference.y, tile->image.width(), tile->image.height()));
        m_counters.cacheReferences += 1;
        ++applied;
    }

    // 3. JPEG atlas tiles.
    for (const TilePlacement &placement : plan.atlasTiles) {
        blit(plan.atlas, QRect(placement.atlasX, placement.atlasY, placement.width, placement.height),
             QPoint(placement.screenX, placement.screenY), false);
        m_frameBuffer.markDirty(QRect(placement.screenX, placement.screenY, placement.width, placement.height));
        m_counters.atlasTiles += 1;
        ++applied;
    }

    // 4. RGB555 rectangle stream.
    if (!plan.pixelRectangles.isEmpty() && !plan.pixelData.isEmpty()) {
        qint64 offset = 0;
        for (const PixelRect &rectangle : plan.pixelRectangles) {
            const QImage tile = rgb555RectToImage(plan.pixelData, offset, rectangle.width, rectangle.height);
            blit(tile, QRect(QPoint(0, 0), tile.size()), QPoint(rectangle.x, rectangle.y), false);
            m_frameBuffer.markDirty(QRect(rectangle.x, rectangle.y, rectangle.width, rectangle.height));
            offset += qint64{rectangle.width} * rectangle.height * 2;
            m_counters.rgb555Tiles += 1;
            ++applied;
        }
    }

    // 5. cache additions (only keys that are not present yet).
    for (const CacheAddition &addition : plan.additions) {
        CachedTile tile;
        tile.image = canvas.copy(QRect(addition.x, addition.y, addition.width, addition.height));
        tile.rectHash = 0;
        // The reference does not count cache additions in its return value.
        if (!m_tileCache.contains(addition.key)) {
            m_tileCache.addIfAbsent(addition.key, std::move(tile));
            m_counters.cacheAdditions += 1;
        }
    }
    const int before = m_tileCache.size();
    m_tileCache.evictToLimit();
    m_counters.cacheEvictions += static_cast<quint64>(qMax(0, before - m_tileCache.size()));

    m_counters.updates += 1;
    return applied;
}

void ScreenDecoder::blit(const QImage &source, const QRect &sourceRect, const QPoint &target,
                         bool allowOutOfBoundsBlack)
{
    QImage &canvas = m_frameBuffer.canvas();
    for (int row = 0; row < sourceRect.height(); ++row) {
        const int destinationRow = target.y() + row;
        if (destinationRow < 0 || destinationRow >= canvas.height())
            continue;
        const int sourceRow = sourceRect.y() + row;
        if (sourceRow < 0 || sourceRow >= source.height())
            continue;
        int columns = sourceRect.width();
        int sourceColumn = sourceRect.x();
        int destinationColumn = target.x();
        if (destinationColumn < 0) {
            columns += destinationColumn;
            sourceColumn -= destinationColumn;
            destinationColumn = 0;
        }
        if (destinationColumn + columns > canvas.width())
            columns = canvas.width() - destinationColumn;
        if (columns <= 0)
            continue;
        if (!allowOutOfBoundsBlack && (sourceColumn < 0 || sourceColumn + columns > source.width()))
            throwProtocol(QStringLiteral("screen copy exceeds the source image"));
        auto *destination = reinterpret_cast<QRgb *>(canvas.scanLine(destinationRow)) + destinationColumn;
        const auto *from = reinterpret_cast<const QRgb *>(source.constScanLine(sourceRow)) + sourceColumn;
        std::memcpy(destination, from, static_cast<size_t>(columns) * 4);
    }
}

ScreenDecoder::Result ScreenDecoder::apply(const QList<QByteArray> &fields)
{
    Plan plan;
    // Validation never mutates the canvas or the cache, so a rejected update
    // leaves both untouched (the reference achieves the same with copies).
    validate(fields, &plan);
    const int applied = applyPlan(plan);

    Result result;
    result.applied = applied;
    result.changed = applied > 0;
    return result;
}

ScreenDecoder::Result ScreenDecoder::applyRgb555(const QList<QByteArray> &fields)
{
    if (fields.size() < 5 || fields.at(0).isEmpty()
        || static_cast<quint8>(fields.at(0).at(0)) != 1) {
        throwProtocol(QStringLiteral("expected kind1 screen update"));
    }
    if (!m_frameBuffer.isValid())
        throwProtocol(QStringLiteral("screen update arrived before the partner desktop size was known"));
    if (fields.at(1).isEmpty() || fields.at(1).size() % 8 != 0)
        throwProtocol(QStringLiteral("expected UInt16 x/y/w/h rectangle records"));
    if (!ZstdDecoder::hasFrameMagic(fields.at(2)))
        throwProtocol(QStringLiteral("screen codec is outside supported Zstandard subset"));
    for (int index = 5; index < fields.size(); ++index) {
        if (!fields.at(index).isEmpty())
            throwUnsupported(QStringLiteral("additional screen operations are not supported yet"),
                             QStringLiteral("field %1").arg(index));
    }

    const QSize canvasSize = m_frameBuffer.size();
    Plan plan;
    qint64 expected = 0;
    for (int offset = 0; offset < fields.at(1).size(); offset += 8) {
        PixelRect rectangle;
        rectangle.x = readUInt16(fields.at(1).constData() + offset);
        rectangle.y = readUInt16(fields.at(1).constData() + offset + 2);
        rectangle.width = readUInt16(fields.at(1).constData() + offset + 4);
        rectangle.height = readUInt16(fields.at(1).constData() + offset + 6);
        if (rectangle.width == 0 || rectangle.height == 0 || rectangle.x + rectangle.width > canvasSize.width()
            || rectangle.y + rectangle.height > canvasSize.height()) {
            throwProtocol(QStringLiteral("screen rectangle outside canvas"),
                          QStringLiteral("(%1,%2 %3x%4)")
                              .arg(rectangle.x).arg(rectangle.y)
                              .arg(rectangle.width).arg(rectangle.height));
        }
        const qint64 pixels = qint64{rectangle.width} * qint64{rectangle.height};
        if (pixels > (std::numeric_limits<qint64>::max() / 2) - expected)
            throwResource(QStringLiteral("screen rectangle pixel count overflows"));
        expected += pixels * 2;
        plan.pixelRectangles.append(rectangle);
    }
    plan.pixelData = m_zstd.decompress(fields.at(2), expected);

    const int applied = applyPlan(plan);
    Result result;
    result.applied = applied;
    result.changed = applied > 0;
    return result;
}

} // namespace ultraview::decoder
