#pragma once
//
// Cursor shape cache (command 3 selection / command 4 transfer).
//
// The host selects shapes by a 32-bit hash.  Shapes it has already sent are
// reused; an unknown hash is requested from the host once and the request is
// remembered so a burst of selections cannot turn into a request storm.
//
// The cache is bounded and evicts the least recently used shape, mirroring the
// screen tile cache policy.  One instance per session; not thread safe.
//
#include "decoder/CursorDecoder.h"

#include <QHash>
#include <QList>

#include <optional>

namespace ultraview::decoder {

class CursorCache {
public:
    static constexpr int kDefaultLimit = 32;

    explicit CursorCache(int limit = kDefaultLimit);

    void setLimit(int limit);
    int limit() const { return m_limit; }
    int size() const { return static_cast<int>(m_shapes.size()); }

    /// Insert or refresh a shape, evicting the least recently used entry.
    void insert(const RemoteCursor &cursor);

    /// Look up a shape and mark it as recently used.
    std::optional<RemoteCursor> find(qint32 hash);

    bool contains(qint32 hash) const { return m_shapes.contains(hash); }

    /// True exactly once per unknown hash: the caller should request the shape
    /// from the host.  Cleared by `insert()` for that hash.
    bool shouldRequest(qint32 hash);
    void clearPendingRequests() { m_requested.clear(); }

    void clear();

private:
    QHash<qint32, RemoteCursor> m_shapes;
    QList<qint32> m_order; // most recently used last
    QHash<qint32, bool> m_requested;
    int m_limit = kDefaultLimit;
};

} // namespace ultraview::decoder
