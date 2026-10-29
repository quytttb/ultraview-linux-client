//
// Tests for the remote cursor pipeline: the Windows `.cur` / DIB decoder
// (command 4) and the bounded shape cache that answers command 3 selections.
//
// The fixtures are built byte by byte here instead of checked in, so the padding,
// bottom-up row order and AND-mask semantics the decoder must implement stay
// visible in the test.
//
#include "TestSupport.h"

#include "core/ErrorTypes.h"
#include "decoder/CursorCache.h"
#include "decoder/CursorDecoder.h"

#include <QtTest>

#include <cstring>

using namespace ultraview;
using namespace ultraview::decoder;

namespace {

void putU16(QByteArray &data, int offset, quint16 value)
{
    data[offset] = static_cast<char>(value & 0xffU);
    data[offset + 1] = static_cast<char>((value >> 8) & 0xffU);
}

void putU32(QByteArray &data, int offset, quint32 value)
{
    for (int index = 0; index < 4; ++index)
        data[offset + index] = static_cast<char>((value >> (8 * index)) & 0xffU);
}

/// Build a cursor resource.  `pixels` is top-down, either ARGB (32 bpp), RGB
/// (24 bpp) or a palette index (1/4/8 bpp); `transparent` is the AND mask.
QByteArray buildCursor(int width, int height, int bpp, const QVector<quint32> &pixels,
                       const QList<QRgb> &palette, const QVector<bool> &transparent,
                       quint16 hotspotX, quint16 hotspotY, quint16 type = 2)
{
    const int xorStride = ((width * bpp + 31) / 32) * 4;
    const int andStride = ((width + 31) / 32) * 4;

    QByteArray xorRows(static_cast<qsizetype>(xorStride) * height, '\0');
    QByteArray andRows(static_cast<qsizetype>(andStride) * height, '\0');

    for (int row = 0; row < height; ++row) {
        const int sourceRow = height - 1 - row; // DIB rows are bottom-up
        for (int column = 0; column < width; ++column) {
            const quint32 value = pixels.at(sourceRow * width + column);
            const qsizetype pixelOffset = static_cast<qsizetype>(row) * xorStride;
            if (bpp == 32) {
                xorRows[pixelOffset + column * 4] = static_cast<char>(value & 0xffU);
                xorRows[pixelOffset + column * 4 + 1] = static_cast<char>((value >> 8) & 0xffU);
                xorRows[pixelOffset + column * 4 + 2] = static_cast<char>((value >> 16) & 0xffU);
                xorRows[pixelOffset + column * 4 + 3] = static_cast<char>((value >> 24) & 0xffU);
            } else if (bpp == 24) {
                xorRows[pixelOffset + column * 3] = static_cast<char>(value & 0xffU);
                xorRows[pixelOffset + column * 3 + 1] = static_cast<char>((value >> 8) & 0xffU);
                xorRows[pixelOffset + column * 3 + 2] = static_cast<char>((value >> 16) & 0xffU);
            } else {
                const int bitOffset = column * bpp;
                const qsizetype byteIndex = pixelOffset + bitOffset / 8;
                const int shift = 8 - bpp - bitOffset % 8;
                const int mask = (1 << bpp) - 1;
                xorRows[byteIndex] = static_cast<char>(
                    (static_cast<quint8>(xorRows.at(byteIndex)) | ((value & mask) << shift)) & 0xffU);
            }
            if (transparent.at(sourceRow * width + column)) {
                const qsizetype maskOffset = static_cast<qsizetype>(row) * andStride;
                andRows[maskOffset + column / 8] = static_cast<char>(
                    static_cast<quint8>(andRows.at(maskOffset + column / 8)) | (1 << (7 - column % 8)));
            }
        }
    }

    QByteArray bitmap(40, '\0');
    putU32(bitmap, 0, 40);
    putU32(bitmap, 4, static_cast<quint32>(width));
    putU32(bitmap, 8, static_cast<quint32>(height * 2));
    putU16(bitmap, 12, 1);
    putU16(bitmap, 14, static_cast<quint16>(bpp));
    putU32(bitmap, 20, static_cast<quint32>(xorRows.size()));
    putU32(bitmap, 32, palette.isEmpty() ? 0U : static_cast<quint32>(palette.size()));
    for (const QRgb colour : palette) {
        bitmap.append(static_cast<char>(qBlue(colour)));
        bitmap.append(static_cast<char>(qGreen(colour)));
        bitmap.append(static_cast<char>(qRed(colour)));
        bitmap.append('\0');
    }
    const QByteArray image = bitmap + xorRows + andRows;

    QByteArray data(22, '\0');
    putU16(data, 0, 0);
    putU16(data, 2, type);
    putU16(data, 4, 1);
    data[6] = static_cast<char>(width <= 256 ? width : 0);
    data[7] = static_cast<char>(height <= 256 ? height : 0);
    putU16(data, 10, hotspotX);
    putU16(data, 12, hotspotY);
    putU32(data, 14, static_cast<quint32>(image.size()));
    putU32(data, 18, 22);
    data.append(image);
    return data;
}

} // namespace

class TestCursor : public QObject {
    Q_OBJECT

private slots:
    void decodes32BitAlphaCursor();
    void decodes1BitPaletteCursorWithMask();
    void decodes4BitPaletteCursor();
    void decodes8BitPaletteCursor();
    void decodes24BitCursorWithMask();
    void clampsTheHotspot();
    void rejectsAnIcon();
    void rejectsTruncatedData();
    void rejectsAnOversizedImage();
    void rejectsAnUnsupportedBitDepth();
    void cacheEvictsTheLeastRecentlyUsed();
    void cacheDeduplicatesRequests();
    void cacheClearResetsEverything();
};

void TestCursor::decodes32BitAlphaCursor()
{
    const QVector<quint32> pixels = {0xffff0000U, 0x8000ff00U, 0x00000000U, 0xff0000ffU};
    const QVector<bool> mask(4, false);
    const RemoteCursor cursor =
        CursorDecoder::decode(buildCursor(2, 2, 32, pixels, {}, mask, 1, 0), 42);
    QVERIFY(cursor.isValid());
    QCOMPARE(cursor.hash, 42);
    QCOMPARE(cursor.hotspot, QPoint(1, 0));
    QCOMPARE(cursor.image.size(), QSize(2, 2));
    QCOMPARE(cursor.image.pixel(0, 0), qRgba(255, 0, 0, 255));
    QCOMPARE(cursor.image.pixel(1, 0), qRgba(0, 255, 0, 0x80));
    // Row 1 is the bottom row of the DIB and must appear at the bottom.
    QCOMPARE(cursor.image.pixel(0, 1), qRgba(0, 0, 0, 0));
    QCOMPARE(cursor.image.pixel(1, 1), qRgba(0, 0, 255, 255));
}

void TestCursor::decodes1BitPaletteCursorWithMask()
{
    const QList<QRgb> palette = {qRgb(0, 0, 0), qRgb(255, 255, 255)};
    // Top row: black, white.  Bottom row: white, black.
    const QVector<quint32> pixels = {0, 1, 1, 0};
    // AND mask = 1 with a black XOR pixel means transparent; with white it means
    // "inverted", which this decoder draws as opaque black.
    const QVector<bool> mask = {true, false, false, false};
    const RemoteCursor cursor =
        CursorDecoder::decode(buildCursor(2, 2, 1, pixels, palette, mask, 0, 0), 7);
    QVERIFY(cursor.isValid());
    QCOMPARE(cursor.image.pixel(0, 0), qRgba(0, 0, 0, 0));
    QCOMPARE(cursor.image.pixel(1, 0), qRgba(255, 255, 255, 255));
    QCOMPARE(cursor.image.pixel(0, 1), qRgba(255, 255, 255, 255));
    QCOMPARE(cursor.image.pixel(1, 1), qRgba(0, 0, 0, 255));
}

void TestCursor::decodes4BitPaletteCursor()
{
    QList<QRgb> palette;
    for (int index = 0; index < 16; ++index)
        palette.append(qRgb(index * 16, index * 16, index * 16));
    const QVector<quint32> pixels = {3, 0, 15, 4};
    const QVector<bool> mask(4, false);
    const RemoteCursor cursor =
        CursorDecoder::decode(buildCursor(2, 2, 4, pixels, palette, mask, 0, 0), 9);
    QVERIFY(cursor.isValid());
    QCOMPARE(cursor.image.pixel(0, 0), qRgba(48, 48, 48, 255));
    QCOMPARE(cursor.image.pixel(0, 1), qRgba(15 * 16, 15 * 16, 15 * 16, 255));
}

void TestCursor::decodes8BitPaletteCursor()
{
    QList<QRgb> palette;
    for (int index = 0; index < 256; ++index)
        palette.append(qRgb(index, 255 - index, 0));
    const QVector<quint32> pixels = {200};
    const QVector<bool> mask = {false};
    const RemoteCursor cursor =
        CursorDecoder::decode(buildCursor(1, 1, 8, pixels, palette, mask, 0, 0), 11);
    QVERIFY(cursor.isValid());
    QCOMPARE(cursor.image.pixel(0, 0), qRgba(200, 55, 0, 255));
}

void TestCursor::decodes24BitCursorWithMask()
{
    // 0x0000ff is red in the packed form used above (B, G, R).
    const QVector<quint32> pixels = {0x000000U, 0xffffffU, 0xff0000U, 0x00ff00U};
    const QVector<bool> mask = {true, false, false, false};
    const RemoteCursor cursor =
        CursorDecoder::decode(buildCursor(2, 2, 24, pixels, {}, mask, 0, 1), 3);
    QVERIFY(cursor.isValid());
    QCOMPARE(cursor.hotspot, QPoint(0, 1));
    // AND = 1 with black XOR -> transparent.
    QCOMPARE(cursor.image.pixel(0, 0), qRgba(0, 0, 0, 0));
    QCOMPARE(cursor.image.pixel(1, 0), qRgba(255, 255, 255, 255));
    QCOMPARE(cursor.image.pixel(0, 1), qRgba(255, 0, 0, 255));
    QCOMPARE(cursor.image.pixel(1, 1), qRgba(0, 255, 0, 255));
}

void TestCursor::clampsTheHotspot()
{
    const QVector<quint32> pixels(4, 0xff112233U);
    const QVector<bool> mask(4, false);
    const RemoteCursor cursor =
        CursorDecoder::decode(buildCursor(2, 2, 32, pixels, {}, mask, 99, 99), 1);
    QCOMPARE(cursor.hotspot, QPoint(1, 1));
}

void TestCursor::rejectsAnIcon()
{
    const QVector<quint32> pixels(4, 0xff112233U);
    const QVector<bool> mask(4, false);
    bool threw = false;
    try {
        CursorDecoder::decode(buildCursor(2, 2, 32, pixels, {}, mask, 0, 0, 1), 1);
    } catch (const UvException &exception) {
        threw = true;
        QCOMPARE(int(exception.code()), int(ErrorCode::Protocol));
    }
    QVERIFY(threw);
}

void TestCursor::rejectsTruncatedData()
{
    bool threw = false;
    try {
        CursorDecoder::decode(QByteArray(5, '\0'), 1);
    } catch (const UvException &exception) {
        threw = true;
        QCOMPARE(int(exception.code()), int(ErrorCode::Protocol));
    }
    QVERIFY(threw);
}

void TestCursor::rejectsAnOversizedImage()
{
    QByteArray data = buildCursor(2, 2, 32, QVector<quint32>(4, 0xff112233U), {},
                                  QVector<bool>(4, false), 0, 0);
    // BITMAPINFOHEADER width lives at offset 22 + 4.
    putU32(data, 26, 300);
    bool threw = false;
    try {
        CursorDecoder::decode(data, 1);
    } catch (const UvException &exception) {
        threw = true;
        QCOMPARE(int(exception.code()), int(ErrorCode::Protocol));
    }
    QVERIFY(threw);
}

void TestCursor::rejectsAnUnsupportedBitDepth()
{
    QByteArray data = buildCursor(2, 2, 32, QVector<quint32>(4, 0xff112233U), {},
                                  QVector<bool>(4, false), 0, 0);
    // BITMAPINFOHEADER bit count lives at offset 22 + 14.
    putU16(data, 36, 7);
    bool threw = false;
    try {
        CursorDecoder::decode(data, 1);
    } catch (const UvException &exception) {
        threw = true;
        QCOMPARE(int(exception.code()), int(ErrorCode::Protocol));
    }
    QVERIFY(threw);
}

namespace {

RemoteCursor makeShape(qint32 hash)
{
    RemoteCursor cursor;
    cursor.hash = hash;
    cursor.image = QImage(2, 2, QImage::Format_ARGB32);
    cursor.image.fill(Qt::black);
    cursor.hotspot = QPoint(0, 0);
    return cursor;
}

} // namespace

void TestCursor::cacheEvictsTheLeastRecentlyUsed()
{
    CursorCache cache(2);
    cache.insert(makeShape(1));
    cache.insert(makeShape(2));
    QVERIFY(cache.contains(1));
    QVERIFY(cache.find(1).has_value()); // 1 is now the most recently used
    cache.insert(makeShape(3));         // must evict 2
    QVERIFY(cache.contains(1));
    QVERIFY(!cache.contains(2));
    QVERIFY(cache.contains(3));
    QCOMPARE(cache.size(), 2);
}

void TestCursor::cacheDeduplicatesRequests()
{
    CursorCache cache;
    QVERIFY(cache.shouldRequest(1234));
    QVERIFY(!cache.shouldRequest(1234));
    cache.insert(makeShape(1234));
    QVERIFY(!cache.shouldRequest(1234));
    QVERIFY(cache.contains(1234));
}

void TestCursor::cacheClearResetsEverything()
{
    CursorCache cache;
    cache.insert(makeShape(5));
    QVERIFY(cache.shouldRequest(6));
    cache.clear();
    QCOMPARE(cache.size(), 0);
    QVERIFY(!cache.contains(5));
    QVERIFY(cache.shouldRequest(6)); // the pending request was forgotten too
}

QTEST_MAIN(TestCursor)
#include "tst_cursor.moc"
