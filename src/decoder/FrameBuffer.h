#pragma once
//
// The remote framebuffer plus the decode-thread -> render-thread hand-off.
//
// Threading contract (required by Qt Quick: textures may only be created in the
// render thread, and QImage must not be mutated while another thread reads it):
//
//   * the decode thread owns the canvas and mutates it,
//   * `takeUpdate()` produces an immutable `FrameUpdate` that owns a *copy* of the
//     changed region only, never a reference to the live canvas,
//   * the GUI/render thread keeps its own mirror image and blits the patch into
//     it on GUI-thread submission; Qt's synchronized render phase reads that
//     mirror inside `updatePaintNode()`.
//
// That keeps every copy bounded by the dirty rectangle while preserving the
// "no shared mutable pixels" rule.
//
#include <QByteArray>
#include <QMetaType>
#include <QRect>
#include <QImage>
#include <QSize>

#include <cstdint>
#include <memory>

namespace ultraview::decoder {

/// Immutable region of the canvas, published to the render thread.
struct FrameUpdate {
    quint64 sequence = 0;
    QSize canvasSize;
    /// Changed region in canvas coordinates.
    QRect rect;
    /// Pixels of `rect` (always QImage::Format_RGB32).
    QImage patch;
    /// True when `rect` covers the whole canvas.
    bool fullFrame = false;
    /// Number of applied screen operations in this batch (statistics only).
    int operations = 0;

    bool isEmpty() const { return rect.isEmpty() || patch.isNull(); }
};

using FrameUpdatePtr = std::shared_ptr<const FrameUpdate>;

class FrameBuffer {
public:
    /// Allocation policy: the reference refuses logins whose desktop exceeds
    /// 32 * 1024 * 1024 pixels (`desktop_size_from_login`), and every rectangle is
    /// validated against the canvas before use.
    static constexpr qint64 kMaxCanvasPixels = 32LL * 1024 * 1024;
    static constexpr int kMaxCanvasEdge = 8192;

    FrameBuffer() = default;

    /// Allocate a black canvas of `size` pixels.  Throws ErrorCode::Resource when
    /// the size is outside the allocation policy.
    void reset(const QSize &size);

    bool isValid() const { return !m_canvas.isNull(); }
    QSize size() const { return m_canvas.size(); }
    QImage &canvas() { return m_canvas; }
    const QImage &canvas() const { return m_canvas; }

    /// Validate and allocate in one step; returns false instead of throwing.
    static bool sizeAllowed(const QSize &size);

    void markDirty(const QRect &rect);
    void markAllDirty();
    QRect dirtyRect() const { return m_dirty; }
    bool hasChanges() const { return !m_dirty.isEmpty(); }

    /// Publish the accumulated change and clear the dirty state.  Returns null
    /// when nothing changed.  The canvas keeps ownership of its pixels.
    FrameUpdatePtr takeUpdate(int operations = 0);

    quint64 sequence() const { return m_sequence; }

    /// SHA-256 of the current canvas bytes; used by tests and diagnostics.
    QByteArray contentHash() const;

    /// The canvas as packed RGB888 bytes, i.e. exactly what the Python reference
    /// produces with `PIL.Image.tobytes()`.
    QByteArray toRgb888() const;

private:
    QImage m_canvas;
    QRect m_dirty;
    quint64 m_sequence = 0;
};

} // namespace ultraview::decoder

// The render hand-off crosses threads (decode worker -> GUI/render thread), so the
// patch pointer must be usable as a queued-connection argument.
Q_DECLARE_METATYPE(ultraview::decoder::FrameUpdatePtr)
