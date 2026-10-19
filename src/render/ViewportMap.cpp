#include "render/ViewportMap.h"

#include <algorithm>

namespace ultraview::render {

ViewportMap::ViewportMap(const QSizeF &itemSize, const QSize &sourceSize, Mode mode)
    : m_itemSize(itemSize)
    , m_sourceSize(sourceSize)
    , m_mode(mode)
{
}

bool ViewportMap::isValid() const
{
    return m_itemSize.width() > 0.0 && m_itemSize.height() > 0.0 && m_sourceSize.width() > 0
        && m_sourceSize.height() > 0;
}

QSizeF ViewportMap::scaleFactors() const
{
    if (!isValid())
        return QSizeF();
    const qreal horizontal = m_itemSize.width() / m_sourceSize.width();
    const qreal vertical = m_itemSize.height() / m_sourceSize.height();
    switch (m_mode) {
    case Mode::Stretch:
        return QSizeF(horizontal, vertical);
    case Mode::PreserveAspectCrop: {
        const qreal uniform = std::max(horizontal, vertical);
        return QSizeF(uniform, uniform);
    }
    case Mode::PreserveAspectFit:
    default: {
        const qreal uniform = std::min(horizontal, vertical);
        return QSizeF(uniform, uniform);
    }
    }
}

QRectF ViewportMap::contentRect() const
{
    if (!isValid())
        return QRectF();
    const QSizeF scale = scaleFactors();
    const qreal width = m_sourceSize.width() * scale.width();
    const qreal height = m_sourceSize.height() * scale.height();
    return QRectF((m_itemSize.width() - width) / 2.0, (m_itemSize.height() - height) / 2.0, width, height);
}

bool ViewportMap::mapItemToSource(const QPointF &itemPoint, QPointF *sourcePoint) const
{
    if (!isValid() || sourcePoint == nullptr)
        return false;
    if (!QRectF(QPointF(0, 0), m_itemSize).contains(itemPoint))
        return false;
    const QRectF content = contentRect();
    if (content.isEmpty() || !content.contains(itemPoint))
        return false;
    const QSizeF scale = scaleFactors();
    if (scale.width() <= 0.0 || scale.height() <= 0.0)
        return false;
    const qreal x = (itemPoint.x() - content.left()) / scale.width();
    const qreal y = (itemPoint.y() - content.top()) / scale.height();
    // `contains` is inclusive on the right/bottom edge; keep the result inside the
    // source so the partner never receives an out-of-desktop coordinate.
    *sourcePoint = QPointF(std::clamp(x, 0.0, qreal(m_sourceSize.width() - 1)),
                           std::clamp(y, 0.0, qreal(m_sourceSize.height() - 1)));
    return true;
}

QPointF ViewportMap::mapSourceToItem(const QPointF &sourcePoint) const
{
    const QRectF content = contentRect();
    const QSizeF scale = scaleFactors();
    return QPointF(content.left() + sourcePoint.x() * scale.width(),
                   content.top() + sourcePoint.y() * scale.height());
}

} // namespace ultraview::render
