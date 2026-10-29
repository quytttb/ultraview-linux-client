#include "decoder/CursorCache.h"

#include <algorithm>

namespace ultraview::decoder {

CursorCache::CursorCache(int limit)
    : m_limit(std::max(1, limit))
{
}

void CursorCache::setLimit(int limit)
{
    m_limit = std::max(1, limit);
    while (m_shapes.size() > m_limit && !m_order.isEmpty()) {
        const qint32 oldest = m_order.takeFirst();
        m_shapes.remove(oldest);
    }
}

void CursorCache::insert(const RemoteCursor &cursor)
{
    if (!cursor.isValid())
        return;
    m_shapes.insert(cursor.hash, cursor);
    m_requested.remove(cursor.hash);
    m_order.removeAll(cursor.hash);
    m_order.append(cursor.hash);
    while (m_order.size() > m_limit)
        m_shapes.remove(m_order.takeFirst());
}

std::optional<RemoteCursor> CursorCache::find(qint32 hash)
{
    const auto iterator = m_shapes.constFind(hash);
    if (iterator == m_shapes.constEnd())
        return std::nullopt;
    m_order.removeAll(hash);
    m_order.append(hash);
    return iterator.value();
}

bool CursorCache::shouldRequest(qint32 hash)
{
    if (m_shapes.contains(hash) || m_requested.contains(hash))
        return false;
    m_requested.insert(hash, true);
    return true;
}

void CursorCache::clear()
{
    m_shapes.clear();
    m_order.clear();
    m_requested.clear();
}

} // namespace ultraview::decoder
