#pragma once
//
// Item <-> remote-desktop coordinate mapping for the viewer.
//
// Kept separate from the render item so the mapping can be unit-tested without a
// scene graph.  The viewer shows the partner desktop either letter-boxed
// (`PreserveAspectFit`), cropped (`PreserveAspectCrop`) or stretched, and pointer
// events have to be translated back into remote desktop pixels; a mapping error
// of one pixel is a visible click offset on the partner side, so the arithmetic
// lives in exactly one place.
//
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>

namespace ultraview::render {

class ViewportMap {
public:
    enum class Mode {
        /// Whole desktop visible, black bars when the aspect ratio differs
        /// (default).
        PreserveAspectFit,
        /// Fills the item, cropping the overflowing axis.
        PreserveAspectCrop,
        /// Fills the item, distorting the aspect ratio.
        Stretch,
    };

    ViewportMap() = default;
    ViewportMap(const QSizeF &itemSize, const QSize &sourceSize, Mode mode = Mode::PreserveAspectFit);

    QSizeF itemSize() const { return m_itemSize; }
    QSize sourceSize() const { return m_sourceSize; }
    Mode mode() const { return m_mode; }
    void setItemSize(const QSizeF &size) { m_itemSize = size; }
    void setSourceSize(const QSize &size) { m_sourceSize = size; }
    void setMode(Mode mode) { m_mode = mode; }

    /// Area of the item that shows the remote desktop, in item coordinates.
    QRectF contentRect() const;

    /// Uniform scale factor of the content (x and y differ only for Stretch).
    QSizeF scaleFactors() const;

    /// Item coordinates -> remote desktop pixel coordinates.  Returns false when
    /// the point is outside the content area or the geometry is degenerate; on
    /// success `sourcePoint` is inside [0, sourceSize).
    bool mapItemToSource(const QPointF &itemPoint, QPointF *sourcePoint) const;

    /// Remote desktop pixels -> item coordinates (inverse of mapItemToSource).
    QPointF mapSourceToItem(const QPointF &sourcePoint) const;

    /// True when the mapping has a non-empty content area.
    bool isValid() const;

private:
    QSizeF m_itemSize;
    QSize m_sourceSize;
    Mode m_mode = Mode::PreserveAspectFit;
};

} // namespace ultraview::render
