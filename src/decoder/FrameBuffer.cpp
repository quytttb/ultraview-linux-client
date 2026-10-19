#include "decoder/FrameBuffer.h"

#include "core/ErrorTypes.h"

#include <QCryptographicHash>

namespace ultraview::decoder {

bool FrameBuffer::sizeAllowed(const QSize &size)
{
    if (size.width() <= 0 || size.height() <= 0)
        return false;
    if (size.width() > kMaxCanvasEdge || size.height() > kMaxCanvasEdge)
        return false;
    return qint64{size.width()} * qint64{size.height()} <= kMaxCanvasPixels;
}

void FrameBuffer::reset(const QSize &size)
{
    if (!sizeAllowed(size)) {
        throwResource(QStringLiteral("partner desktop dimensions are outside the allocation limit"),
                      QStringLiteral("%1x%2").arg(size.width()).arg(size.height()));
    }
    // Format_RGB32 matches the recovered 32-bit RGB canvas and gives a fast,
    // conversion-free texture upload path in Qt Quick.
    m_canvas = QImage(size, QImage::Format_RGB32);
    m_canvas.fill(Qt::black);
    m_dirty = QRect(QPoint(0, 0), size);
    m_sequence = 0;
}

void FrameBuffer::markDirty(const QRect &rect)
{
    if (!m_canvas.isNull())
        m_dirty = m_dirty.united(rect.intersected(QRect(QPoint(0, 0), m_canvas.size())));
}

void FrameBuffer::markAllDirty()
{
    if (!m_canvas.isNull())
        m_dirty = QRect(QPoint(0, 0), m_canvas.size());
}

FrameUpdatePtr FrameBuffer::takeUpdate(int operations)
{
    if (m_canvas.isNull() || m_dirty.isEmpty())
        return {};

    const QRect full(QPoint(0, 0), m_canvas.size());
    const QRect rect = m_dirty.intersected(full);
    m_dirty = QRect();
    if (rect.isEmpty())
        return {};

    auto update = std::make_shared<FrameUpdate>();
    update->sequence = ++m_sequence;
    update->canvasSize = m_canvas.size();
    update->rect = rect;
    update->patch = m_canvas.copy(rect);
    update->fullFrame = (rect == full);
    update->operations = operations;
    return update;
}

QByteArray FrameBuffer::contentHash() const
{
    if (m_canvas.isNull())
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const QImage rgb888 = m_canvas.convertToFormat(QImage::Format_RGB888);
    const qsizetype rowBytes = qsizetype{rgb888.width()} * 3;
    for (int row = 0; row < rgb888.height(); ++row)
        hash.addData(QByteArrayView(rgb888.constScanLine(row), rowBytes));
    return hash.result();
}

QByteArray FrameBuffer::toRgb888() const
{
    if (m_canvas.isNull())
        return {};
    const QImage rgb888 = m_canvas.convertToFormat(QImage::Format_RGB888);
    // Packed rows: `bytesPerLine()` may carry alignment padding that the reference
    // (PIL `Image.tobytes()`) never produces, so only width * 3 bytes per row are
    // part of the pixel stream.
    const qsizetype rowBytes = qsizetype{rgb888.width()} * 3;
    QByteArray out;
    out.reserve(rowBytes * rgb888.height());
    for (int row = 0; row < rgb888.height(); ++row)
        out.append(reinterpret_cast<const char *>(rgb888.constScanLine(row)), rowBytes);
    return out;
}

} // namespace ultraview::decoder
