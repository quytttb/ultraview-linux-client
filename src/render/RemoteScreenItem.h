#pragma once
//
// The remote screen, rendered by a custom scene-graph node.
//
// Design (Phase 5 requirement):
//   * `RemoteScreenItem` is a plain `QQuickItem`; all pixels are drawn by
//     `updatePaintNode()` through a `QSGSimpleTextureNode`, so there is no
//     QQuickPaintedItem, no framebuffer object and no image in the QML layer,
//   * the decode worker owns the authoritative canvas; it publishes immutable
//     dirty-rect patches (`decoder::FrameUpdate`), which are *copied* into this
//     item's mirror, so no pixel buffer is ever shared mutably across threads,
//   * the mirror is uploaded with the public QtQuick API
//     (`QQuickWindow::createTextureFromImage`) only when patches arrived since
//     the previous frame; several patches coalesce into one upload.  A partial
//     (dirty-region) GPU upload would need Qt's private RHI headers, which are
//     deliberately not a build dependency,
//   * the patch list is bounded: if the scene graph does not consume it (hidden
//     window, no RHI backend), it is collapsed into one full re-upload instead
//     of growing without limit,
//   * a resolution change is only accepted as a full frame (the mirror is
//     reallocated, queued patches of the previous canvas are dropped), and a
//     patch is only applied when it fits the mirror completely -- refused
//     updates are still acknowledged so the decode worker never stalls.
//
#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QQuickItem>

QT_BEGIN_NAMESPACE
class QSGSimpleTextureNode;
QT_END_NAMESPACE

#include "decoder/FrameBuffer.h"
#include "render/ViewportMap.h"

namespace ultraview::render {

class RemoteScreenItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QSize sourceSize READ sourceSize NOTIFY sourceSizeChanged)
    Q_PROPERTY(QRectF contentRect READ contentRect NOTIFY contentRectChanged)
    Q_PROPERTY(FillMode fillMode READ fillMode WRITE setFillMode NOTIFY fillModeChanged)
    Q_PROPERTY(bool smooth READ isSmooth WRITE setSmooth NOTIFY smoothChanged)
    Q_PROPERTY(bool hasContent READ hasContent NOTIFY sourceSizeChanged)

public:
    enum class FillMode {
        PreserveAspectFit = 0,
        PreserveAspectCrop = 1,
        Stretch = 2,
    };
    Q_ENUM(FillMode)

    explicit RemoteScreenItem(QQuickItem *parent = nullptr);
    ~RemoteScreenItem() override;

    QSize sourceSize() const { return m_sourceSize; }
    QRectF contentRect() const;
    FillMode fillMode() const { return m_fillMode; }
    void setFillMode(FillMode mode);
    bool isSmooth() const { return m_smooth; }
    void setSmooth(bool smooth);

    /// Number of patches that have not reached the GPU yet (diagnostics/tests).
    int pendingTexturePatches() const { return static_cast<int>(m_texturePending.size()); }
    /// SHA-256 of the mirror, i.e. what the user sees (diagnostics/tests).
    Q_INVOKABLE QByteArray mirrorHash() const;
    /// The mirror as packed RGB888, exactly like the decoder's canvas dump.
    Q_INVOKABLE QByteArray mirrorRgb888() const;
    /// True once a patch has been received.
    bool hasContent() const { return !m_mirror.isNull(); }

    /// Item coordinates -> remote desktop pixel coordinates; (-1, -1) when the
    /// point is outside the displayed desktop.
    Q_INVOKABLE QPointF mapToSource(qreal itemX, qreal itemY) const;
    /// Remote desktop pixels -> item coordinates.
    Q_INVOKABLE QPointF mapFromSource(qreal sourceX, qreal sourceY) const;

public slots:
    /// Accept one decoded patch.  Called from the GUI thread through a queued
    /// connection; never blocks and never throws.
    void submitUpdate(const ultraview::decoder::FrameUpdatePtr &update);
    /// Drop all pixels (disconnect / reconnect).
    void clear();
    /// Commands 3/4: the partner's cursor shape, drawn over the desktop at the
    /// position from command 28.  An invalid image hides it.
    void setRemoteCursor(const QImage &image, const QPoint &hotspot);
    /// Command 28: pointer position in partner desktop pixels.
    void setCursorPosition(const QPoint &position);
    /// Show or hide the partner cursor independently of the shape (the user's
    /// "show remote cursor" preference).
    void setRemoteCursorVisible(bool visible);

signals:
    void sourceSizeChanged();
    void contentRectChanged();
    void fillModeChanged();
    void smoothChanged();
    /// One patch has been taken over by the renderer; the worker uses this to
    /// keep at most a couple of patches in flight.
    void framesConsumed(int count);

protected:
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    ViewportMap viewportMap() const;
    QRect contentRectI() const;
    void blitIntoMirror(const QRect &rect, const QImage &patch);
    QSGNode *updateCursorNode(QSGNode *root, QSGSimpleTextureNode *cursorNode);

    QImage m_mirror;
    QSize m_sourceSize;
    /// Rectangles of the patches that changed the mirror since the last frame;
    /// the mirror is re-uploaded in full when the list is non-empty.  Bounded by
    /// `kMaxTexturePatches` so a stalled renderer cannot grow it without limit.
    QList<QRect> m_texturePending;
    bool m_textureReset = false;
    bool m_forceUpload = false;
    FillMode m_fillMode = FillMode::PreserveAspectFit;
    bool m_smooth = true;
    QRect m_lastContentRect;

    // Remote cursor overlay.  The image is scaled with the desktop, so the
    // hotspot stays exactly under the pointer at every zoom level.
    QImage m_cursorImage;
    QPoint m_cursorHotspot;
    QPoint m_cursorPosition;
    bool m_cursorPositionValid = false;
    bool m_cursorVisible = true;
};

} // namespace ultraview::render
