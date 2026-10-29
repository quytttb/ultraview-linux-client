#pragma once
//
// Remote hardware cursor decoding (commands 3, 4 and 28).
//
// The host transmits a 32-bit hash of the active cursor shape (command 3) and,
// on a cache miss, the complete Windows `.cur` resource (command 4) compressed
// with plain Zstandard.  This decoder turns that resource into a Qt image plus
// its hotspot; the shape cache and the on-demand requests live in the session.
//
// `.cur` structure (see docs/protocol.md):
//
//   ICONDIR        6 bytes   reserved u16 = 0, type u16 = 2, count u16
//   ICONDIRENTRY  16 bytes   w, h, colors, reserved, hotspot x, hotspot y,
//                            bytesInRes, imageOffset
//   image data              BITMAPINFOHEADER/BITMAPCOREHEADER, palette,
//                            XOR bitmap (colors), AND bitmap (1 bpp mask)
//
// `biHeight` is twice the cursor height because the XOR and AND bitmaps are
// stacked.  1, 4, 8, 16, 24 and 32 bpp are supported.
//
// Mask truth table (Windows): AND 0 = opaque pixel from the XOR bitmap,
// AND 1 + XOR black = transparent, AND 1 + XOR white = inverted.  A composition
// cannot invert the screen, so an inverted pixel is drawn as opaque black, which
// stays visible on the light backgrounds where these cursors are used.
//
#include <QByteArray>
#include <QImage>
#include <QPoint>

#include <cstdint>

namespace ultraview::decoder {

struct RemoteCursor {
    QImage image;
    QPoint hotspot;
    qint32 hash = 0;

    bool isValid() const { return !image.isNull(); }
};

class CursorDecoder {
public:
    /// Selection hash the host uses to mean "no change / hidden pointer".
    static constexpr qint32 kIgnoredSelectionHash = static_cast<qint32>(0xb84d1a70u);
    /// Windows cursors are at most 256x256.
    static constexpr int kMaxCursorSize = 256;
    /// Bound for one decompressed `.cur` resource.
    static constexpr qint64 kMaxCurBytes = 1024 * 1024;

    /// Decode a `.cur` resource.  Throws UvException (ErrorCode::Protocol) for any
    /// structural problem; never returns a partially decoded image.
    static RemoteCursor decode(const QByteArray &data, qint32 hash);
};

} // namespace ultraview::decoder
