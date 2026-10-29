#include "decoder/TileCache.h"

#include "core/ErrorTypes.h"
#include <QCoreApplication>
#include "decoder/FrameBuffer.h"
#include <cstring>

namespace ultraview::decoder {
namespace {

qint32 readInt32(const QByteArray &data, int offset)
{
    const uchar *bytes = reinterpret_cast<const uchar *>(data.constData() + offset);
    const quint32 value = static_cast<quint32>(bytes[0]) | (static_cast<quint32>(bytes[1]) << 8)
        | (static_cast<quint32>(bytes[2]) << 16) | (static_cast<quint32>(bytes[3]) << 24);
    return static_cast<qint32>(value);
}

void appendInt32(QByteArray &out, qint32 value)
{
    const quint32 raw = static_cast<quint32>(value);
    out.append(static_cast<char>(raw & 0xFF));
    out.append(static_cast<char>((raw >> 8) & 0xFF));
    out.append(static_cast<char>((raw >> 16) & 0xFF));
    out.append(static_cast<char>((raw >> 24) & 0xFF));
}

} // namespace

TileCache::TileCache(int limit)
{
    setLimit(limit);
}

void TileCache::setLimit(int limit)
{
    if (limit < 1)
        throwError(ErrorCode::Configuration, QCoreApplication::translate("Decoder", "cache limit must be positive"));
    m_limit = limit;
    evictToLimit();
}

void TileCache::clear()
{
    m_entries.clear();
    m_index.clear();
    m_bytes = 0;
}

const CachedTile *TileCache::peek(qint32 key) const
{
    const auto found = m_index.constFind(key);
    if (found == m_index.constEnd())
        return nullptr;
    return &(*found)->tile;
}

const CachedTile *TileCache::reference(qint32 key)
{
    const auto found = m_index.find(key);
    if (found == m_index.end())
        return nullptr;
    // Reference behaviour: `cache.move_to_end(key)` on every reference.
    m_entries.splice(m_entries.end(), m_entries, *found);
    return &(*found)->tile;
}

void TileCache::addIfAbsent(qint32 key, CachedTile tile)
{
    if (m_index.contains(key))
        return;
    if (tile.image.isNull() || tile.image.sizeInBytes() > kMaxCacheBytes - m_bytes)
        throwResource(QCoreApplication::translate("Decoder", "tile cache exceeds memory budget"));
    m_bytes += tile.image.sizeInBytes();
    m_entries.push_back(Entry{key, std::move(tile)});
    auto iterator = m_entries.end();
    --iterator;
    m_index.insert(key, iterator);
}

void TileCache::evictToLimit()
{
    while (size() > m_limit) {
        auto front = m_entries.begin();
        m_bytes -= front->tile.image.sizeInBytes();
        m_index.remove(front->key);
        m_entries.erase(front);
    }
}

QList<qint32> TileCache::keysInOrder() const
{
    QList<qint32> keys;
    keys.reserve(static_cast<int>(m_entries.size()));
    for (const Entry &entry : m_entries)
        keys.append(entry.key);
    return keys;
}

QByteArray TileCache::serializeItem(qint32 width, qint32 height, qint32 rectHash, const QByteArray &data)
{
    QByteArray out;
    out.reserve(16 + data.size());
    appendInt32(out, width);
    appendInt32(out, height);
    appendInt32(out, rectHash);
    appendInt32(out, static_cast<qint32>(data.size()));
    out.append(data);
    return out;
}

TileCache::ItemHeader TileCache::parseItem(const QByteArray &payload)
{
    if (payload.size() < 16)
        throwProtocol(QCoreApplication::translate("Decoder", "truncated cache header"));
    ItemHeader header;
    header.width = readInt32(payload, 0);
    header.height = readInt32(payload, 4);
    header.rectHash = readInt32(payload, 8);
    const qint32 size = readInt32(payload, 12);
    if (size < 0 || payload.size() != qint64{16} + size)
        throwProtocol(QCoreApplication::translate("Decoder", "invalid cache payload length"));
    // Stricter than the reference's CachedItem.parse, which accepts a zero-sized
    // header: such an entry can never be a tile, and decodeTile() would reject it
    // one step later anyway.
    if (header.width <= 0 || header.height <= 0)
        throwProtocol(QCoreApplication::translate("Decoder", "invalid cached tile dimensions"));
    header.data = payload.mid(16);
    return header;
}

QByteArray TileCache::encodeTile(const QImage &image)
{
    if (image.isNull())
        return {};
    const QImage rgb888 = image.convertToFormat(QImage::Format_RGB888);
    // Packed rows without the alignment padding of Format_RGB888, so that
    // decodeTile() can validate the payload size exactly.
    const qsizetype rowBytes = qsizetype{rgb888.width()} * 3;
    QByteArray out;
    out.reserve(rowBytes * rgb888.height());
    for (int row = 0; row < rgb888.height(); ++row)
        out.append(reinterpret_cast<const char *>(rgb888.constScanLine(row)), rowBytes);
    return out;
}

QImage TileCache::decodeTile(qint32 width, qint32 height, const QByteArray &payload)
{
    if (!FrameBuffer::sizeAllowed(QSize(width, height)))
        throwProtocol(QCoreApplication::translate("Decoder", "invalid cached tile dimensions"));
    const qint64 expected = qint64{width} * qint64{height} * 3;
    if (expected != payload.size())
        throwProtocol(QCoreApplication::translate("Decoder", "cached tile payload has an unexpected size"),
                      QStringLiteral("%1 != %2").arg(payload.size()).arg(expected));
    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull())
        throwResource(QCoreApplication::translate("Decoder", "cannot allocate a cached tile"));
    for (int row = 0; row < height; ++row)
        std::memcpy(image.scanLine(row), payload.constData() + qint64{row} * width * 3,
                    static_cast<size_t>(width) * 3);
    return image.convertToFormat(QImage::Format_RGB32);
}

QByteArray TileCache::serialize() const
{
    QByteArray out;
    for (const Entry &entry : m_entries) {
        const QByteArray payload = serializeItem(entry.tile.image.width(), entry.tile.image.height(),
                                                entry.tile.rectHash, encodeTile(entry.tile.image));
        appendInt32(out, entry.key);
        appendInt32(out, static_cast<qint32>(payload.size()));
        out.append(payload);
    }
    return out;
}

TileCache TileCache::fromSerialized(const QByteArray &data, int limit)
{
    TileCache cache(limit);
    int offset = 0;
    while (offset < data.size()) {
        if (data.size() - offset < 8)
            throwProtocol(QCoreApplication::translate("Decoder", "truncated cache outer header"));
        const qint32 key = readInt32(data, offset);
        const qint32 size = readInt32(data, offset + 4);
        offset += 8;
        if (size < 0 || size > data.size() - offset || cache.contains(key))
            throwProtocol(QCoreApplication::translate("Decoder", "invalid cache entry size or duplicate key"));
        const ItemHeader header = parseItem(data.mid(offset, size));
        offset += size;
        CachedTile tile;
        tile.rectHash = header.rectHash;
        tile.image = decodeTile(header.width, header.height, header.data);
        cache.addIfAbsent(key, std::move(tile));
    }
    return cache;
}

} // namespace ultraview::decoder
