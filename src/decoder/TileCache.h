#pragma once
//
// Screen tile cache and the recovered on-disk container format.
//
// The reference (`screen_decode.RGB555Screen`) keeps an `OrderedDict` of tiles
// with a limit of 1000 entries, moves a key to the end whenever it is referenced,
// evicts from the front and only stores a tile when its key is not present yet.
// Field 9 of a kind-1 update references those tiles, field 10 adds them.
//
// `uv_reference.CachedItem`/`export_cache`/`import_cache` describe the *container*
// the original writes to disk (method 0x060011e6/0x060011e7).  The container
// layout is pinned byte-exactly by `tests/data/fixtures.json` ("cache_wire"), but
// the *tile payload* encoding was not recovered, so the native client:
//   * reads and writes the recovered container (key + CachedItem header + data),
//   * uses its own documented payload (packed RGB888 rows) for tiles it wrote,
//   * refuses a payload whose size does not match the declared dimensions with an
//     explicit ErrorCode::Protocol instead of rendering misinterpreted pixels.
// A cache exported by the original Windows client therefore imports as an explicit
// error, which docs/compatibility.md lists as an unsupported operation.
//
#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QList>

#include <list>

#include <cstdint>

namespace ultraview::decoder {

/// One tile of the cache.
struct CachedTile {
    QImage image;
    /// Recovered `CachedItem.rect_hash`; the drawing path does not compute it, so
    /// the native client stores 0 unless a value is supplied.
    qint32 rectHash = 0;
};

class TileCache {
public:
    /// `cache_limit=1000` as in the reference constructor.
    static constexpr int kDefaultLimit = 1000;
    static constexpr qint64 kMaxCacheBytes = 256LL * 1024 * 1024;

    explicit TileCache(int limit = kDefaultLimit);
    TileCache(const TileCache &) = delete;
    TileCache &operator=(const TileCache &) = delete;
    TileCache(TileCache &&) = default;
    TileCache &operator=(TileCache &&) = default;
    qint64 bytes() const { return m_bytes; }

    int limit() const { return m_limit; }
    void setLimit(int limit);
    int size() const { return static_cast<int>(m_entries.size()); }
    void clear();

    bool contains(qint32 key) const { return m_index.contains(key); }

    /// Tile for `key` without changing the eviction order; nullptr when unknown.
    const CachedTile *peek(qint32 key) const;

    /// Tile for `key`, and (like the reference) move the key to the end of the
    /// eviction order.  Returns nullptr when the key is unknown.
    const CachedTile *reference(qint32 key);

    /// Store `tile` only when `key` is not present yet (reference behaviour:
    /// `if key not in cache: cache[key] = ...`).
    void addIfAbsent(qint32 key, CachedTile tile);

    /// Enforce the limit by dropping the least recently referenced entries.
    void evictToLimit();

    /// Keys in eviction order (front = next to be evicted).
    QList<qint32> keysInOrder() const;

    // --- recovered container format (CachedItem / export_cache) ------------

    /// `CachedItem.serialize()`: `<Int32 width><Int32 height><Int32 rect_hash><Int32 size><data>`.
    static QByteArray serializeItem(qint32 width, qint32 height, qint32 rectHash, const QByteArray &data);

    struct ItemHeader {
        qint32 width = 0;
        qint32 height = 0;
        qint32 rectHash = 0;
        QByteArray data;
    };
    /// `CachedItem.parse()` with the reference's validation.
    static ItemHeader parseItem(const QByteArray &payload);

    /// `export_cache`: `<Int32 key><Int32 payload size><CachedItem>` per entry.
    QByteArray serialize() const;

    /// `import_cache`: strict validation, atomic (nothing is modified on error).
    /// Throws ErrorCode::Protocol on truncation, negative size or duplicate key.
    static TileCache fromSerialized(const QByteArray &data, int limit = kDefaultLimit);

    /// Payload of one tile in the native client's own encoding (packed RGB888).
    static QByteArray encodeTile(const QImage &image);
    static QImage decodeTile(qint32 width, qint32 height, const QByteArray &payload);

private:
    struct Entry {
        qint32 key = 0;
        CachedTile tile;
    };

    int m_limit;
    qint64 m_bytes = 0;
    std::list<Entry> m_entries;
    QHash<qint32, std::list<Entry>::iterator> m_index;
};

} // namespace ultraview::decoder
