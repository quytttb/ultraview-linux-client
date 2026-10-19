#include "render/RemoteScreenItem.h"

#include <QCryptographicHash>
#include <QImage>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

#include <cstring>

namespace ultraview::render {
namespace {

/// Bounded number of patches that may wait for the scene graph.  Beyond that the
/// whole mirror is re-uploaded on the next frame instead of queueing more.
constexpr int kMaxTexturePatches = 32;

} // namespace

RemoteScreenItem::RemoteScreenItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(QQuickItem::ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAntialiasing(false);
}

RemoteScreenItem::~RemoteScreenItem() = default;

ViewportMap RemoteScreenItem::viewportMap() const
{
    ViewportMap map(QSizeF(width(), height()), m_sourceSize, static_cast<ViewportMap::Mode>(m_fillMode));
    return map;
}

QRect RemoteScreenItem::contentRectI() const
{
    return viewportMap().contentRect().toAlignedRect();
}

QRectF RemoteScreenItem::contentRect() const
{
    return viewportMap().contentRect();
}

void RemoteScreenItem::setFillMode(FillMode mode)
{
    if (m_fillMode == mode)
        return;
    m_fillMode = mode;
    emit fillModeChanged();
    emit contentRectChanged();
    update();
}

void RemoteScreenItem::setSmooth(bool smooth)
{
    if (m_smooth == smooth)
        return;
    m_smooth = smooth;
    emit smoothChanged();
    update();
}

void RemoteScreenItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() == oldGeometry.size())
        return;
    const QRect rect = contentRectI();
    if (rect != m_lastContentRect) {
        m_lastContentRect = rect;
        emit contentRectChanged();
    }
}

QPointF RemoteScreenItem::mapToSource(qreal itemX, qreal itemY) const
{
    QPointF source;
    if (!viewportMap().mapItemToSource(QPointF(itemX, itemY), &source))
        return QPointF(-1, -1);
    return source;
}

QPointF RemoteScreenItem::mapFromSource(qreal sourceX, qreal sourceY) const
{
    return viewportMap().mapSourceToItem(QPointF(sourceX, sourceY));
}

QByteArray RemoteScreenItem::mirrorHash() const
{
    if (m_mirror.isNull())
        return {};
    const QImage rgb888 = m_mirror.convertToFormat(QImage::Format_RGB888);
    // Packed rows: `bytesPerLine()` may carry alignment padding that the decoder's
    // canvas dump never contains, so only width * 3 bytes per row are hashed.
    const qsizetype rowBytes = qsizetype{rgb888.width()} * 3;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (int row = 0; row < rgb888.height(); ++row)
        hash.addData(QByteArrayView(rgb888.constScanLine(row), rowBytes));
    return hash.result();
}

QByteArray RemoteScreenItem::mirrorRgb888() const
{
    if (m_mirror.isNull())
        return {};
    const QImage rgb888 = m_mirror.convertToFormat(QImage::Format_RGB888);
    const qsizetype rowBytes = qsizetype{rgb888.width()} * 3;
    QByteArray out;
    out.reserve(rowBytes * rgb888.height());
    for (int row = 0; row < rgb888.height(); ++row)
        out.append(reinterpret_cast<const char *>(rgb888.constScanLine(row)), rowBytes);
    return out;
}

void RemoteScreenItem::blitIntoMirror(const QRect &rect, const QImage &patch)
{
    if (m_mirror.isNull() || patch.isNull())
        return;
    // `submitUpdate` refuses anything that does not fit; the check is repeated
    // here so a future caller cannot draw outside the mirror either.
    if (rect.width() <= 0 || rect.height() <= 0 || patch.size() != rect.size()
        || !m_mirror.rect().contains(rect))
        return;
    // Both sides are Format_RGB32, so a row copy is exact and cheap.
    const int bytes = rect.width() * 4;
    for (int row = 0; row < rect.height(); ++row) {
        std::memcpy(m_mirror.scanLine(rect.top() + row) + qsizetype(rect.left()) * 4,
                    patch.constScanLine(row), size_t(bytes));
    }
}

void RemoteScreenItem::submitUpdate(const ultraview::decoder::FrameUpdatePtr &frame)
{
    if (!frame || frame->isEmpty())
        return;
    if (!decoder::FrameBuffer::sizeAllowed(frame->canvasSize)
        || frame->patch.format() != QImage::Format_RGB32) {
        emit framesConsumed(1);
        return;
    }

    if (frame->canvasSize != m_sourceSize) {
        // The partner changed resolution: only a full frame may replace the
        // mirror, because a partial patch cannot describe a canvas the renderer
        // has never seen.  A refused update is still acknowledged so the worker's
        // in-flight window stays open.
        if (!frame->fullFrame || frame->rect != QRect(QPoint(0, 0), frame->canvasSize)
            || frame->patch.size() != frame->canvasSize) {
            emit framesConsumed(1);
            return;
        }
        m_sourceSize = frame->canvasSize;
        m_mirror = QImage(m_sourceSize, QImage::Format_RGB32);
        m_mirror.fill(Qt::black);
        m_texturePending.clear();
        // No reset flag: the scene graph re-uploads because the texture still has
        // the previous canvas size (`updatePaintNode`), and blocking the queue
        // here would delay every patch of the new canvas.
        m_textureReset = false;
        m_forceUpload = true;
        blitIntoMirror(frame->rect, frame->patch);
        const QRect rect = contentRectI();
        m_lastContentRect = rect;
        emit sourceSizeChanged();
        emit contentRectChanged();
        emit framesConsumed(1);
        QQuickItem::update();
        return;
    }

    if (m_mirror.isNull()) {
        // No canvas yet: a patch for a size the worker never announced cannot be
        // placed.  Acknowledge it so the worker is not blocked forever.
        emit framesConsumed(1);
        return;
    }

    // A patch must fit the mirror completely and carry exactly its own pixels;
    // clipping it would draw parts of the image at coordinates the worker did
    // not send them for.
    if (frame->patch.size() != frame->rect.size() || !m_mirror.rect().contains(frame->rect)) {
        emit framesConsumed(1);
        return;
    }

    blitIntoMirror(frame->rect, frame->patch);

    if (!m_textureReset) {
        if (m_texturePending.size() >= kMaxTexturePatches) {
            // The scene graph is not consuming patches (hidden window, blocked
            // renderer); fall back to one full upload instead of unbounded growth.
            m_texturePending.clear();
            m_textureReset = true;
        } else {
            m_texturePending.append(frame->rect);
        }
    }

    // The renderer owns the patch now: the worker may publish the next one.
    emit framesConsumed(1);
    QQuickItem::update();
}

void RemoteScreenItem::clear()
{
    m_mirror = QImage();
    m_sourceSize = QSize();
    m_texturePending.clear();
    // Force the next paint to upload: a reconnect can produce a canvas of the
    // same size as the one the (stale) texture was created from.
    m_textureReset = true;
    m_forceUpload = true;
    const QRect rect = contentRectI();
    m_lastContentRect = rect;
    emit sourceSizeChanged();
    emit contentRectChanged();
    update();
}

QSGNode *RemoteScreenItem::updatePaintNode(QSGNode *node, UpdatePaintNodeData *data)
{
    Q_UNUSED(data);
    QSGSimpleTextureNode *textureNode = static_cast<QSGSimpleTextureNode *>(node);
    if (m_mirror.isNull()) {
        delete textureNode;
        return nullptr;
    }
    if (textureNode == nullptr) {
        textureNode = new QSGSimpleTextureNode();
        textureNode->setOwnsTexture(true);
    }

    // The texture is created from the mirror whenever it changed since the last
    // frame.  Only the public QtQuick API is used (`createTextureFromImage`);
    // partial GPU uploads would require Qt's private RHI headers, so patches are
    // coalesced into one re-upload per frame instead (see docs/architecture.md).
    const bool needsUpload = m_forceUpload || m_textureReset || !m_texturePending.isEmpty()
        || textureNode->texture() == nullptr
        || textureNode->texture()->textureSize() != m_mirror.size();
    if (needsUpload) {
        if (QQuickWindow *quickWindow = window()) {
            if (QSGTexture *uploaded = quickWindow->createTextureFromImage(
                    m_mirror, QQuickWindow::TextureIsOpaque)) {
                textureNode->setTexture(uploaded); // ownership transfers (setOwnsTexture(true))
                m_textureReset = false;
                m_forceUpload = false;
                m_texturePending.clear();
            }
            // No texture yet (no RHI backend): the bookkeeping is kept so the next
            // frame retries; the pending list stays bounded by kMaxTexturePatches.
        }
    }

    if (QSGTexture *texture = textureNode->texture()) {
        textureNode->setFiltering(m_smooth ? QSGTexture::Linear : QSGTexture::Nearest);
        texture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
        texture->setVerticalWrapMode(QSGTexture::ClampToEdge);
    } else {
        delete textureNode;
        return nullptr; // never return an untextured QSGSimpleTextureNode
    }

    const QRectF content = contentRect();
    textureNode->setRect(content.isEmpty() ? QRectF(0, 0, width(), height()) : content);

    // contentRect already expresses the crop transform. Sample the entire
    // texture and let the viewport clip overflow; cropping the source as well
    // would apply the transform twice and disagree with pointer mapping.
    textureNode->setSourceRect(QRectF(0, 0, m_mirror.width(), m_mirror.height()));
    return textureNode;
}

} // namespace ultraview::render
