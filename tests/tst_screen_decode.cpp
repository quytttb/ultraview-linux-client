//
// Parity tests for the screen decoder, the tile cache and the JPEG atlas path.
//
// `tests/data/fixtures.json` carries eight `screen_sequences` produced by the
// Python reference (`screen_decode.RGB555Screen`), each with the encoded packet,
// the number of applied operations and the SHA-256 of the resulting RGB canvas.
// The `8bit.dat` artifact cannot be redistributed, so the RGB555 sequences are
// decompressed with the synthetic dictionary `tests/data/test-dict.bin` that the
// generator trained for exactly these samples.
//
#include "TestSupport.h"

#include "core/ErrorTypes.h"
#include "decoder/FrameBuffer.h"
#include "decoder/JpegAtlasDecoder.h"
#include "decoder/ScreenDecoder.h"
#include "decoder/TileCache.h"
#include "protocol/PacketCodec.h"

#include <QtTest>

using namespace ultraview;
using namespace ultraview::decoder;
using namespace ultraview::protocol;

namespace {

QList<QByteArray> packetFields(const QByteArray &packet)
{
    return decodeByteArrayFields(packet, 11);
}

QList<QRect> rectanglesFromFixture(const QJsonArray &value)
{
    QList<QRect> rectangles;
    for (const QJsonValue &rectangle : value) {
        const QJsonArray numbers = rectangle.toArray();
        // Each fixture rectangle is exactly [x, y, w, h].
        if (numbers.size() != 4)
            return QList<QRect>();
        rectangles.append(QRect(numbers.at(0).toInt(), numbers.at(1).toInt(), numbers.at(2).toInt(),
                                numbers.at(3).toInt()));
    }
    return rectangles;
}

} // namespace

class TestScreenDecode : public QObject {
    Q_OBJECT

private slots:
    void canvasInitialisation();
    void desktopDimensions();
    void rgb555Sequences();
    void jpegAtlasSequence();
    void atlasLayout();
    void atlasRejectsWrongBlockSize();
    void cacheContainerFormat();
    void cacheEvictionSemantics();
    void cacheRoundTripThroughContainer();
    void frameUpdateHandoff();
};

void TestScreenDecode::canvasInitialisation()
{
    FrameBuffer buffer;
    QVERIFY(!buffer.isValid());
    QVERIFY(!FrameBuffer::sizeAllowed(QSize(0, 10)));
    QVERIFY(!FrameBuffer::sizeAllowed(QSize(8192, 8192)));
    QVERIFY_EXCEPTION_THROWN(buffer.reset(QSize(0, 10)), UvException);

    buffer.reset(QSize(8, 4));
    QVERIFY(buffer.isValid());
    QCOMPARE(buffer.size(), QSize(8, 4));
    QVERIFY(buffer.hasChanges());
    QCOMPARE(buffer.dirtyRect(), QRect(0, 0, 8, 4));
    QCOMPARE(buffer.canvas().pixel(0, 0), qRgb(0, 0, 0));
    QCOMPARE(buffer.toRgb888().size(), 8 * 4 * 3);

    // The first published patch covers the whole canvas and owns its pixels.
    const FrameUpdatePtr update = buffer.takeUpdate(0);
    QVERIFY(update != nullptr);
    QVERIFY(update->fullFrame);
    QCOMPARE(update->rect, QRect(0, 0, 8, 4));
    QCOMPARE(update->canvasSize, QSize(8, 4));
    QCOMPARE(update->patch.size(), QSize(8, 4));
    QCOMPARE(buffer.takeUpdate(0), FrameUpdatePtr());
}

void TestScreenDecode::desktopDimensions()
{
    const auto int32le = [](qint32 number) {
        QByteArray out(4, '\0');
        const quint32 bits = static_cast<quint32>(number);
        out[0] = static_cast<char>(bits & 0xFFu);
        out[1] = static_cast<char>((bits >> 8) & 0xFFu);
        out[2] = static_cast<char>((bits >> 16) & 0xFFu);
        out[3] = static_cast<char>((bits >> 24) & 0xFFu);
        return out;
    };

    const QJsonObject fixture = uvtest::Fixtures::instance().object(QStringLiteral("desktop_dims"));
    for (const QJsonValue &value : fixture.value(QStringLiteral("valid")).toArray()) {
        const QJsonObject entry = value.toObject();
        const int width = entry.value(QStringLiteral("width")).toInt();
        const int height = entry.value(QStringLiteral("height")).toInt();
        QVERIFY(entry.value(QStringLiteral("accepted")).toBool());
        QVERIFY(FrameBuffer::sizeAllowed(QSize(width, height)));
        // Fields 1 and 2 of a kind-0 acknowledgement are Int32LE dimensions.
        const QList<QByteArray> fields{QByteArray(1, '\0'), int32le(width), int32le(height)};
        QCOMPARE(fields.at(1).size(), 4);
        QCOMPARE(ScreenDecoder::desktopSizeFromLogin(fields), QSize(width, height));
    }
    // Little endian, checked against the raw bytes of a known value.
    QCOMPARE(int32le(1024), uvtest::fromHex(QStringLiteral("00040000")));
    QCOMPARE(int32le(768), uvtest::fromHex(QStringLiteral("00030000")));
    for (const QJsonValue &value : fixture.value(QStringLiteral("invalid")).toArray()) {
        const QJsonObject entry = value.toObject();
        const int width = entry.value(QStringLiteral("width")).toInt();
        const int height = entry.value(QStringLiteral("height")).toInt();
        QVERIFY(!entry.value(QStringLiteral("accepted")).toBool());
        QVERIFY(!FrameBuffer::sizeAllowed(QSize(width, height)));
        QVERIFY_EXCEPTION_THROWN(ScreenDecoder::desktopSizeFromLogin(
                                     {QByteArray(1, '\0'), int32le(width), int32le(height)}),
                                 UvException);
    }
}

void TestScreenDecode::rgb555Sequences()
{
    const QJsonArray sequences = uvtest::Fixtures::instance().array(QStringLiteral("screen_sequences"));
    QVERIFY(sequences.size() >= 7);
    for (const QJsonValue &value : sequences) {
        const QJsonObject sequence = value.toObject();
        const QString name = sequence.value(QStringLiteral("name")).toString();
        if (name == QLatin1String("jpeg_atlas_block32"))
            continue; // lossy: compared through samples below

        ScreenDecoder decoder;
        decoder.setOptions(ScreenDecoder::Options{sequence.value(QStringLiteral("block_size")).toInt(),
                                                  TileCache::kDefaultLimit,
                                                  ZstdDecoder::kDefaultMaxOutputBytes});
        decoder.setDictionary(uvtest::Fixtures::instance().dictionary());

        const QJsonArray canvas = sequence.value(QStringLiteral("canvas")).toArray();
        const QSize size(canvas.at(0).toInt(), canvas.at(1).toInt());
        decoder.resetCanvas(size);
        QCOMPARE(decoder.frameBuffer().size(), size);

        int index = 0;
        for (const QJsonValue &updateValue : sequence.value(QStringLiteral("updates")).toArray()) {
            const QJsonObject update = updateValue.toObject();
            const QList<QByteArray> fields =
                packetFields(uvtest::Fixtures::entryBytes(update, QStringLiteral("packet_hex")));
            QCOMPARE(fields.size(), 11);
            const QByteArray hashBefore = decoder.frameBuffer().contentHash();
            const QByteArray pixelsBefore = decoder.frameBuffer().toRgb888();
            const auto cacheBefore = decoder.cache().keysInOrder();

            if (update.value(QStringLiteral("error")).toBool()) {
                // A rejected update must leave the canvas *and* the cache untouched
                // (the reference works on copies and only commits on success).
                QVERIFY_EXCEPTION_THROWN(decoder.apply(fields), UvException);
                QCOMPARE(decoder.frameBuffer().contentHash(), hashBefore);
                QCOMPARE(decoder.frameBuffer().toRgb888(), pixelsBefore);
                QCOMPARE(decoder.cache().keysInOrder(), cacheBefore);
            } else {
                const ScreenDecoder::Result result = decoder.apply(fields);
                QCOMPARE(result.applied, update.value(QStringLiteral("applied")).toInt());
                if (update.contains(QStringLiteral("canvas_sha256"))) {
                    QCOMPARE(QString::fromLatin1(decoder.frameBuffer().contentHash().toHex()),
                             update.value(QStringLiteral("canvas_sha256")).toString());
                }
                if (update.contains(QStringLiteral("canvas_hex"))) {
                    // Despite the key name the reference serializes the canvas as
                    // base64 (`base64.b64encode(image.tobytes())`).
                    const QByteArray expected =
                        QByteArray::fromBase64(update.value(QStringLiteral("canvas_hex")).toString().toLatin1());
                    QCOMPARE(decoder.frameBuffer().toRgb888().size(), expected.size());
                    QCOMPARE(decoder.frameBuffer().toRgb888(), expected);
                }
            }
            if (update.contains(QStringLiteral("cache_keys"))) {
                QStringList keys;
                for (const qint32 key : decoder.cache().keysInOrder())
                    keys.append(QString::number(key));
                keys.sort();
                QStringList expected;
                for (const QJsonValue &key : update.value(QStringLiteral("cache_keys")).toArray())
                    expected.append(key.toString());
                expected.sort();
                QCOMPARE(keys, expected);
            }
            ++index;
        }
        QCOMPARE(index, sequence.value(QStringLiteral("updates")).toArray().size());
    }
}

void TestScreenDecode::jpegAtlasSequence()
{
    const QJsonArray sequences = uvtest::Fixtures::instance().array(QStringLiteral("screen_sequences"));
    for (const QJsonValue &value : sequences) {
        const QJsonObject sequence = value.toObject();
        if (sequence.value(QStringLiteral("name")).toString() != QLatin1String("jpeg_atlas_block32"))
            continue;

        ScreenDecoder decoder;
        const int blockSize = sequence.value(QStringLiteral("block_size")).toInt();
        decoder.setOptions(ScreenDecoder::Options{blockSize, TileCache::kDefaultLimit,
                                                 ZstdDecoder::kDefaultMaxOutputBytes});
        const QJsonArray canvas = sequence.value(QStringLiteral("canvas")).toArray();
        decoder.resetCanvas(QSize(canvas.at(0).toInt(), canvas.at(1).toInt()));

        const QJsonObject update = sequence.value(QStringLiteral("updates")).toArray().at(0).toObject();
        const QList<QByteArray> fields =
            packetFields(uvtest::Fixtures::entryBytes(update, QStringLiteral("packet_hex")));
        QCOMPARE(fields.at(7), uvtest::Fixtures::entryBytes(update, QStringLiteral("jpeg_hex")));
        const ScreenDecoder::Result result = decoder.apply(fields);
        QCOMPARE(result.applied, update.value(QStringLiteral("applied")).toInt());

        // JPEG is lossy, so the reference samples are compared with a tolerance.
        const QJsonArray samples = sequence.value(QStringLiteral("samples")).toArray();
        for (int sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex) {
            const QJsonObject sample = samples.at(sampleIndex).toObject();
            const QJsonArray point = sample.value(QStringLiteral("point")).toArray();
            const QJsonArray expected = sample.value(QStringLiteral("rgb555")).toArray();
            const QColor actual = decoder.frameBuffer().canvas().pixelColor(point.at(0).toInt(),
                                                                           point.at(1).toInt());
            const auto report = [&](const char *channel, int value) {
                return QStringLiteral("%1 sample %2 at %3x%4: %5 %6, expected rgb(%7,%8,%9), actual rgb(%10,%11,%12)")
                                      .arg(QLatin1String(channel))
                                      .arg(sampleIndex)
                                      .arg(point.at(0).toInt())
                                      .arg(point.at(1).toInt())
                                      .arg(QLatin1String(channel))
                                      .arg(value)
                                      .arg(expected.at(0).toInt())
                                      .arg(expected.at(1).toInt())
                                      .arg(expected.at(2).toInt())
                                      .arg(actual.red())
                                      .arg(actual.green())
                                      .arg(actual.blue());
            };
            QVERIFY2(qAbs(actual.red() - expected.at(0).toInt()) <= 24, qPrintable(report("red", actual.red())));
            QVERIFY2(qAbs(actual.green() - expected.at(1).toInt()) <= 24, qPrintable(report("green", actual.green())));
            QVERIFY2(qAbs(actual.blue() - expected.at(2).toInt()) <= 24, qPrintable(report("blue", actual.blue())));
        }
        return;
    }
    QFAIL("the jpeg_atlas_block32 sequence is missing from the fixtures");
}

void TestScreenDecode::atlasLayout()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("atlas_layout"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        const QList<QRect> rectangles = rectanglesFromFixture(entry.value(QStringLiteral("rectangles")).toArray());
        const QList<TilePlacement> tiles = JpegAtlasDecoder::layout(rectangles,
                                                                   entry.value(QStringLiteral("block_size")).toInt(),
                                                                   entry.value(QStringLiteral("atlas_height")).toInt());
        const QJsonArray expected = entry.value(QStringLiteral("tiles")).toArray();
        QCOMPARE(tiles.size(), expected.size());
        for (int index = 0; index < expected.size(); ++index) {
            const QJsonArray numbers = expected.at(index).toArray();
            QCOMPARE(tiles.at(index).screenX, numbers.at(0).toInt());
            QCOMPARE(tiles.at(index).screenY, numbers.at(1).toInt());
            QCOMPARE(tiles.at(index).width, numbers.at(2).toInt());
            QCOMPARE(tiles.at(index).height, numbers.at(3).toInt());
            QCOMPARE(tiles.at(index).atlasX, numbers.at(4).toInt());
            QCOMPARE(tiles.at(index).atlasY, numbers.at(5).toInt());
        }
    }
    // Rectangle parsing (field 8) is Int16LE x/y/w/h quadruples.
    QCOMPARE(JpegAtlasDecoder::parseRectangles(QByteArrayLiteral("\x20\x00\x00\x00\x20\x00\x20\x00")),
             QList<QRect>{QRect(32, 0, 32, 32)});
    QVERIFY_EXCEPTION_THROWN(JpegAtlasDecoder::parseRectangles(QByteArrayLiteral("\x01\x00")), UvException);
    QVERIFY_EXCEPTION_THROWN(JpegAtlasDecoder::loadBottomUpAtlas(QByteArrayLiteral("not a jpeg")), UvException);
}

void TestScreenDecode::atlasRejectsWrongBlockSize()
{
    const QJsonArray sequences = uvtest::Fixtures::instance().array(QStringLiteral("screen_sequences"));
    for (const QJsonValue &value : sequences) {
        const QJsonObject sequence = value.toObject();
        if (sequence.value(QStringLiteral("name")).toString() != QLatin1String("jpeg_atlas_block32"))
            continue;
        QVERIFY(sequence.value(QStringLiteral("wrong_block_size_error")).toBool());
        ScreenDecoder decoder;
        decoder.setOptions(ScreenDecoder::Options{16, TileCache::kDefaultLimit,
                                                 ZstdDecoder::kDefaultMaxOutputBytes});
        decoder.resetCanvas(QSize(96, 32));
        const QList<QByteArray> fields = packetFields(uvtest::Fixtures::entryBytes(
            sequence.value(QStringLiteral("updates")).toArray().at(0).toObject(),
            QStringLiteral("packet_hex")));
        QVERIFY_EXCEPTION_THROWN(decoder.apply(fields), UvException);
        return;
    }
    QFAIL("the jpeg_atlas_block32 sequence is missing from the fixtures");
}

void TestScreenDecode::cacheContainerFormat()
{
    const QJsonObject fixture = uvtest::Fixtures::instance().object(QStringLiteral("cache_wire"));
    const QJsonObject item = fixture.value(QStringLiteral("item")).toObject();
    const QByteArray serialized = uvtest::Fixtures::entryBytes(item, QStringLiteral("serialized_hex"));
    QCOMPARE(TileCache::serializeItem(item.value(QStringLiteral("width")).toInt(),
                                      item.value(QStringLiteral("height")).toInt(),
                                      item.value(QStringLiteral("rect_hash")).toInt(),
                                      uvtest::Fixtures::entryBytes(item, QStringLiteral("data_hex"))),
             serialized);

    const TileCache::ItemHeader header = TileCache::parseItem(serialized);
    QCOMPARE(header.width, item.value(QStringLiteral("width")).toInt());
    QCOMPARE(header.height, item.value(QStringLiteral("height")).toInt());
    QCOMPARE(header.rectHash, item.value(QStringLiteral("rect_hash")).toInt());
    QCOMPARE(header.data, uvtest::Fixtures::entryBytes(item, QStringLiteral("data_hex")));

    // The outer entry is `<Int32 key><Int32 payload size>`; both the recovered
    // layout and its rejection rules are covered here.
    const QByteArray exported = uvtest::Fixtures::entryBytes(fixture, QStringLiteral("export_hex"));
    QCOMPARE(exported.size(), 8 + serialized.size());
    QCOMPARE(exported.left(4), uvtest::fromHex(QStringLiteral("7b000000")));
    QCOMPARE(exported.mid(4, 4), [&serialized] {
        QByteArray size(4, '\0');
        const quint32 value = static_cast<quint32>(serialized.size());
        size[0] = static_cast<char>(value & 0xFFu);
        size[1] = static_cast<char>((value >> 8) & 0xFFu);
        size[2] = static_cast<char>((value >> 16) & 0xFFu);
        size[3] = static_cast<char>((value >> 24) & 0xFFu);
        return size;
    }());
    QVERIFY_EXCEPTION_THROWN(TileCache::parseItem(serialized.left(8)), UvException);
    QVERIFY_EXCEPTION_THROWN(TileCache::parseItem(QByteArray(16, '\0')), UvException);
}

void TestScreenDecode::cacheEvictionSemantics()
{
    TileCache cache(3);
    QCOMPARE(cache.limit(), 3);
    QCOMPARE(cache.size(), 0);

    const auto tile = [](int size) {
        CachedTile value;
        value.image = QImage(size, size, QImage::Format_RGB32);
        value.image.fill(Qt::red);
        return value;
    };
    cache.addIfAbsent(1, tile(1));
    cache.addIfAbsent(2, tile(2));
    cache.addIfAbsent(3, tile(3));
    QCOMPARE(cache.keysInOrder(), QList<qint32>({1, 2, 3}));
    // Adding an existing key does not replace the stored tile.
    CachedTile replacement;
    replacement.image = QImage(1, 1, QImage::Format_RGB32);
    replacement.image.fill(Qt::blue);
    cache.addIfAbsent(1, replacement);
    QCOMPARE(cache.size(), 3);
    QVERIFY(cache.peek(1) != nullptr);
    QCOMPARE(cache.peek(1)->image.pixel(0, 0), qRgb(255, 0, 0));

    // Referencing moves the key to the end of the eviction order.
    QVERIFY(cache.reference(1) != nullptr);
    QCOMPARE(cache.keysInOrder(), QList<qint32>({2, 3, 1}));
    QVERIFY(cache.peek(99) == nullptr);
    QVERIFY(cache.reference(99) == nullptr);

    cache.addIfAbsent(4, tile(4));
    cache.evictToLimit();
    QCOMPARE(cache.size(), 3);
    QCOMPARE(cache.keysInOrder(), QList<qint32>({3, 1, 4}));
    QCOMPARE(cache.keysInOrder().first(), 3); // least recently referenced

    cache.clear();
    QCOMPARE(cache.size(), 0);
    QVERIFY_EXCEPTION_THROWN(cache.setLimit(0), UvException);
}

void TestScreenDecode::cacheRoundTripThroughContainer()
{
    TileCache cache(8);
    CachedTile tile;
    tile.image = QImage(2, 1, QImage::Format_RGB32);
    tile.image.setPixel(0, 0, qRgb(1, 2, 3));
    tile.image.setPixel(1, 0, qRgb(250, 251, 252));
    tile.rectHash = -7;
    cache.addIfAbsent(123, tile);

    const QByteArray exported = cache.serialize();
    // The native payload is packed RGB888 (6 bytes for 2x1), so the entry is
    // 8 + 16 + 6 bytes long; the container layout is the recovered one.
    QCOMPARE(exported.size(), 8 + 16 + 6);
    QCOMPARE(exported.left(4), uvtest::fromHex(QStringLiteral("7b000000")));

    const TileCache restored = TileCache::fromSerialized(exported, 8);
    QCOMPARE(restored.size(), 1);
    const CachedTile *found = restored.peek(123);
    QVERIFY(found != nullptr);
    QCOMPARE(found->rectHash, -7);
    QCOMPARE(found->image.size(), QSize(2, 1));
    QCOMPARE(found->image.pixel(0, 0), qRgb(1, 2, 3));
    QCOMPARE(found->image.pixel(1, 0), qRgb(250, 251, 252));
    QCOMPARE(restored.serialize(), exported);

    // A payload written by another implementation (the reference fixture uses a
    // 2-byte tile for a 2x1 image) has a payload encoding this client never
    // recovered: it is refused explicitly instead of being misread.
    const QByteArray foreign = uvtest::Fixtures::entryBytes(uvtest::Fixtures::instance().object(
                                                                 QStringLiteral("cache_wire")),
                                                             QStringLiteral("export_hex"));
    QVERIFY(!foreign.isEmpty());
    QVERIFY_EXCEPTION_THROWN(TileCache::fromSerialized(foreign, 8), UvException);
    // Duplicate keys and truncated entries are protocol violations.
    QVERIFY_EXCEPTION_THROWN(TileCache::fromSerialized(exported + exported, 8), UvException);
    QVERIFY_EXCEPTION_THROWN(TileCache::fromSerialized(exported.left(10), 8), UvException);
    QVERIFY_EXCEPTION_THROWN(TileCache::fromSerialized(QByteArray(4, '\0'), 8), UvException);
}

void TestScreenDecode::frameUpdateHandoff()
{
    FrameBuffer buffer;
    buffer.reset(QSize(16, 8));
    QVERIFY(buffer.takeUpdate(1) != nullptr);
    QVERIFY(buffer.takeUpdate(1) == nullptr);

    buffer.markDirty(QRect(4, 2, 4, 3));
    const FrameUpdatePtr update = buffer.takeUpdate(2);
    QVERIFY(update != nullptr);
    QVERIFY(!update->fullFrame);
    QCOMPARE(update->rect, QRect(4, 2, 4, 3));
    QCOMPARE(update->patch.size(), QSize(4, 3));
    QCOMPARE(update->operations, 2);
    QVERIFY(!update->isEmpty());
    // The patch owns a copy: mutating the canvas afterwards cannot change it.
    buffer.canvas().fill(Qt::green);
    QCOMPARE(update->patch.pixel(0, 0), qRgb(0, 0, 0));

    // Overlapping dirty rectangles coalesce into one patch (never a queue).
    buffer.markDirty(QRect(0, 0, 4, 4));
    buffer.markDirty(QRect(2, 2, 4, 4));
    QCOMPARE(buffer.dirtyRect(), QRect(0, 0, 6, 6));

    // Marking outside the canvas is clipped to it.
    buffer.markDirty(QRect(-5, -5, 3, 3));
    buffer.markAllDirty();
    QCOMPARE(buffer.dirtyRect(), QRect(0, 0, 16, 8));
    QVERIFY(buffer.takeUpdate(0) != nullptr);
    QCOMPARE(buffer.sequence(), 3u);
}

QTEST_MAIN(TestScreenDecode)
#include "tst_screen_decode.moc"
