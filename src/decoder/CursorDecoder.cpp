#include "decoder/CursorDecoder.h"

#include "core/ErrorTypes.h"
#include <QCoreApplication>

#include <QList>
#include <QVector>

#include <cstring>

namespace ultraview::decoder {
namespace {

quint16 readU16(const QByteArray &data, qsizetype offset)
{
    if (offset < 0 || offset + 2 > data.size())
        throwProtocol(QCoreApplication::translate("Decoder", "cursor resource is truncated"));
    return static_cast<quint16>(static_cast<quint8>(data.at(offset))
                                | (static_cast<quint16>(static_cast<quint8>(data.at(offset + 1))) << 8));
}

qint16 readI16(const QByteArray &data, qsizetype offset)
{
    return static_cast<qint16>(readU16(data, offset));
}

quint32 readU32(const QByteArray &data, qsizetype offset)
{
    if (offset < 0 || offset + 4 > data.size())
        throwProtocol(QCoreApplication::translate("Decoder", "cursor resource is truncated"));
    quint32 value = 0;
    for (int index = 3; index >= 0; --index)
        value = (value << 8) | static_cast<quint8>(data.at(offset + index));
    return value;
}

qint32 readI32(const QByteArray &data, qsizetype offset)
{
    return static_cast<qint32>(readU32(data, offset));
}

void requireRange(qsizetype offset, qsizetype length, qsizetype available, const QString &what)
{
    if (offset < 0 || length < 0 || offset + length > available) {
        throwProtocol(QCoreApplication::translate("Decoder", "cursor resource is truncated"),
                      QStringLiteral("%1 needs bytes %2..%3 of %4")
                          .arg(what)
                          .arg(offset)
                          .arg(offset + length)
                          .arg(available));
    }
}

struct DirectoryEntry {
    int width = 0;
    int height = 0;
    quint16 hotspotX = 0;
    quint16 hotspotY = 0;
    quint32 bytesInResource = 0;
    quint32 imageOffset = 0;
};

QList<DirectoryEntry> readDirectory(const QByteArray &data)
{
    requireRange(0, 6, data.size(), "ICONDIR");
    if (readU16(data, 0) != 0)
        throwProtocol(QCoreApplication::translate("Decoder", "cursor resource has a non-zero reserved field"));
    if (readU16(data, 2) != 2)
        throwProtocol(QCoreApplication::translate("Decoder", "the resource is an icon, not a cursor"));
    const int count = readU16(data, 4);
    if (count <= 0 || count > 64)
        throwProtocol(QCoreApplication::translate("Decoder", "cursor resource has an unusable image count"),
                      QString::number(count));
    requireRange(6, qsizetype(count) * 16, data.size(), "ICONDIRENTRY table");

    QList<DirectoryEntry> entries;
    entries.reserve(count);
    for (int index = 0; index < count; ++index) {
        const qsizetype base = 6 + qsizetype(index) * 16;
        DirectoryEntry entry;
        const int rawWidth = static_cast<quint8>(data.at(base));
        const int rawHeight = static_cast<quint8>(data.at(base + 1));
        entry.width = rawWidth == 0 ? CursorDecoder::kMaxCursorSize : rawWidth;
        entry.height = rawHeight == 0 ? CursorDecoder::kMaxCursorSize : rawHeight;
        entry.hotspotX = readU16(data, base + 4);
        entry.hotspotY = readU16(data, base + 6);
        entry.bytesInResource = readU32(data, base + 8);
        entry.imageOffset = readU32(data, base + 12);
        entries.append(entry);
    }
    return entries;
}

} // namespace

RemoteCursor CursorDecoder::decode(const QByteArray &data, qint32 hash)
{
    if (data.size() < 6)
        throwProtocol(QCoreApplication::translate("Decoder", "cursor resource is empty"));
    if (data.size() > kMaxCurBytes)
        throwError(ErrorCode::Resource, QCoreApplication::translate("Decoder", "cursor resource is too large"));

    const QList<DirectoryEntry> entries = readDirectory(data);
    // The vendor ships a single image; pick the first entry that fits the
    // supported size instead of trusting the directory blindly.
    const DirectoryEntry *selected = nullptr;
    for (const DirectoryEntry &entry : entries) {
        if (entry.width > 0 && entry.width <= kMaxCursorSize && entry.height > 0
            && entry.height <= kMaxCursorSize && entry.bytesInResource > 0) {
            selected = &entry;
            break;
        }
    }
    if (selected == nullptr)
        throwProtocol(QCoreApplication::translate("Decoder", "cursor resource has no usable image"));

    const qsizetype base = static_cast<qsizetype>(selected->imageOffset);
    const qsizetype length = static_cast<qsizetype>(selected->bytesInResource);
    requireRange(base, length, data.size(), QCoreApplication::translate("Decoder", "cursor image data"));

    const quint32 headerSize = readU32(data, base);
    if (headerSize < 12 || headerSize > 124 || headerSize > static_cast<quint32>(length))
        throwProtocol(QCoreApplication::translate("Decoder", "cursor image has an unsupported bitmap header"),
                      QString::number(headerSize));

    const bool core = headerSize == 12;
    int width = 0;
    int height = 0;
    int bits = 0;
    quint32 colorsUsed = 0;
    bool topDown = false;
    if (core) {
        width = readI16(data, base + 4);
        height = readI16(data, base + 6);
        bits = readU16(data, base + 10);
        if (height > 0)
            height /= 2; // core headers store the doubled cursor height as well
    } else {
        width = readI32(data, base + 4);
        const qint32 rawHeight = readI32(data, base + 8);
        topDown = rawHeight < 0;
        const qint32 magnitude = rawHeight < 0 ? -rawHeight : rawHeight;
        height = magnitude % 2 == 0 ? magnitude / 2 : magnitude;
        bits = readU16(data, base + 14);
        colorsUsed = readU32(data, base + 32);
    }

    if (width <= 0 || height <= 0 || width > kMaxCursorSize || height > kMaxCursorSize)
        throwProtocol(QCoreApplication::translate("Decoder", "cursor image has an unsupported size"),
                      QStringLiteral("%1x%2").arg(width).arg(height));
    if (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32)
        throwProtocol(QCoreApplication::translate("Decoder", "cursor image has an unsupported bit depth"),
                      QString::number(bits));

    const qsizetype paletteEntrySize = core ? 3 : 4;
    qsizetype paletteCount = 0;
    if (bits <= 8) {
        paletteCount = colorsUsed != 0 ? static_cast<qsizetype>(colorsUsed)
                                       : (qsizetype(1) << bits);
        if (paletteCount > (qsizetype(1) << bits))
            throwProtocol(QCoreApplication::translate("Decoder", "cursor palette is larger than the bit depth allows"));
    }

    const qsizetype xorStride = ((qsizetype(width) * bits + 31) / 32) * 4;
    const qsizetype andStride = ((qsizetype(width) + 31) / 32) * 4;
    const qsizetype xorOffset = base + static_cast<qsizetype>(headerSize)
        + paletteCount * paletteEntrySize;
    const qsizetype andOffset = xorOffset + xorStride * height;
    requireRange(xorOffset, xorStride * height, base + length, "XOR bitmap");
    requireRange(andOffset, andStride * height, base + length, "AND mask");

    // A palette is read once and converted to RGB, so per-pixel work is a lookup.
    QVector<QRgb> palette;
    if (bits <= 8) {
        palette.reserve(static_cast<int>(paletteCount));
        for (qsizetype index = 0; index < paletteCount; ++index) {
            const qsizetype entry = base + static_cast<qsizetype>(headerSize) + index * paletteEntrySize;
            if (entry + paletteEntrySize > base + length)
                break;
            const int blue = static_cast<quint8>(data.at(entry));
            const int green = static_cast<quint8>(data.at(entry + 1));
            const int red = static_cast<quint8>(data.at(entry + 2));
            palette.append(qRgb(red, green, blue));
        }
        if (palette.isEmpty())
            throwProtocol(QCoreApplication::translate("Decoder", "cursor palette is empty"));
    }

    // 32 bpp resources either carry a real alpha channel or leave it at zero and
    // rely on the AND mask; detect which one this image uses.
    bool usesAlpha = false;
    if (bits == 32) {
        for (int row = 0; row < height && !usesAlpha; ++row) {
            const qsizetype rowOffset = xorOffset + qsizetype(row) * xorStride;
            for (int column = 0; column < width; ++column) {
                if (static_cast<quint8>(data.at(rowOffset + qsizetype(column) * 4 + 3)) != 0) {
                    usesAlpha = true;
                    break;
                }
            }
        }
    }

    QImage image(width, height, QImage::Format_ARGB32);
    if (image.isNull())
        throwError(ErrorCode::Resource, QCoreApplication::translate("Decoder", "cursor image could not be allocated"));

    for (int row = 0; row < height; ++row) {
        // DIB rows are bottom-up unless the height was negative.
        const int sourceRow = topDown ? row : height - 1 - row;
        const qsizetype xorRow = xorOffset + qsizetype(sourceRow) * xorStride;
        const qsizetype andRow = andOffset + qsizetype(sourceRow) * andStride;
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(row));
        for (int column = 0; column < width; ++column) {
            int red = 0;
            int green = 0;
            int blue = 0;
            int alpha = 255;
            switch (bits) {
            case 32: {
                const qsizetype pixel = xorRow + qsizetype(column) * 4;
                blue = static_cast<quint8>(data.at(pixel));
                green = static_cast<quint8>(data.at(pixel + 1));
                red = static_cast<quint8>(data.at(pixel + 2));
                if (usesAlpha)
                    alpha = static_cast<quint8>(data.at(pixel + 3));
                break;
            }
            case 24: {
                const qsizetype pixel = xorRow + qsizetype(column) * 3;
                blue = static_cast<quint8>(data.at(pixel));
                green = static_cast<quint8>(data.at(pixel + 1));
                red = static_cast<quint8>(data.at(pixel + 2));
                break;
            }
            case 16: {
                const qsizetype pixel = xorRow + qsizetype(column) * 2;
                const quint16 value = static_cast<quint16>(
                    static_cast<quint8>(data.at(pixel))
                    | (static_cast<quint16>(static_cast<quint8>(data.at(pixel + 1))) << 8));
                const int r5 = (value >> 10) & 0x1f;
                const int g5 = (value >> 5) & 0x1f;
                const int b5 = value & 0x1f;
                // Same 5-bit expansion the screen decoder uses.
                red = (r5 << 3) | (r5 >> 2);
                green = (g5 << 3) | (g5 >> 2);
                blue = (b5 << 3) | (b5 >> 2);
                break;
            }
            case 8:
            case 4:
            case 1: {
                const int bitsPerPixel = bits;
                const qsizetype byteIndex = xorRow + (qsizetype(column) * bitsPerPixel) / 8;
                const int shift = 8 - bitsPerPixel - (column * bitsPerPixel) % 8;
                const int mask = (1 << bitsPerPixel) - 1;
                const int index = (static_cast<quint8>(data.at(byteIndex)) >> shift) & mask;
                if (index >= palette.size())
                    throwProtocol(QCoreApplication::translate("Decoder", "cursor pixel uses a palette entry that is missing"));
                const QRgb colour = palette.at(index);
                red = qRed(colour);
                green = qGreen(colour);
                blue = qBlue(colour);
                break;
            }
            default:
                break;
            }

            if (bits != 32 || !usesAlpha) {
                const int andShift = 7 - (column % 8);
                const bool masked = ((static_cast<quint8>(data.at(andRow + column / 8)) >> andShift) & 1) != 0;
                if (masked) {
                    if (red == 0 && green == 0 && blue == 0) {
                        alpha = 0;
                    } else if (red == 255 && green == 255 && blue == 255) {
                        // Inverted pixel: draw a solid dark pixel so the cursor
                        // stays visible on a light desktop.
                        red = 0;
                        green = 0;
                        blue = 0;
                        alpha = 255;
                    }
                }
            }
            line[column] = qRgba(red, green, blue, alpha);
        }
    }

    RemoteCursor cursor;
    cursor.image = image;
    cursor.hotspot = QPoint(std::min<int>(selected->hotspotX, width - 1),
                            std::min<int>(selected->hotspotY, height - 1));
    cursor.hash = hash;
    return cursor;
}

} // namespace ultraview::decoder
