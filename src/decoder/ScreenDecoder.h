#pragma once
//
// Screen update decoder: the native port of `screen_decode.RGB555Screen`.
//
// Field map of a kind-1 update, as recovered (VRecordReader.ManipulatePacket):
//
//   0  discriminator byte, 0x01
//   1  RGB555 rectangle records   <UInt16 x, y, w, h>*
//   2  Zstandard pixel stream     reference top-down RGB555, one run per rectangle
//   3  (unused by the observed path)
//   4  (unused by the observed path)
//   5  BitBlt records             -> refused, never silently ignored
//   6  scroll copy                <Int16 delta, x, y, w, h><Int16 rx, ry, rw, rh>*
//   7  JPEG atlas                 bottom-up JPEG bytes
//   8  atlas rectangles           <Int16 x, y, w, h>*
//   9  cache references           <Int16 x, y><Int32 key>*
//  10  cache additions            <Int16 x, y, w, h><Int32 key>*
//
// Application order is exactly the reference's: scroll copy (reading the
// pre-update canvas), cache references, JPEG atlas, RGB555 stream, cache
// additions, then LRU eviction.
//
// Error semantics: the reference is *atomic* -- it works on copies of the canvas
// and the cache and only commits when the whole update succeeded
// (`test_screen_decode.py::test_invalid_later_rectangle_does_not_modify_canvas`).
// The native decoder keeps that guarantee with a validate-then-apply design
// instead of copying the whole canvas for every packet, because every failure
// path of the reference is detected before the first mutation (field lengths,
// rectangle bounds, missing cache keys, atlas geometry, decompression).  Any
// future screen operation that cannot be pre-validated must be added to the
// validation phase first; docs/porting-plan.md records this rule.
//
#include <QByteArray>
#include <QList>
#include <QSize>

#include "decoder/FrameBuffer.h"
#include "decoder/JpegAtlasDecoder.h"
#include "decoder/TileCache.h"
#include "decoder/ZstdDecoder.h"

namespace ultraview::decoder {

class ScreenDecoder {
public:
    struct Options {
        /// JPEG atlas block size (`block_size=32` in the reference CLI).
        int blockSize = 32;
        int cacheLimit = TileCache::kDefaultLimit;
        qint64 maxOutputBytes = ZstdDecoder::kDefaultMaxOutputBytes;
    };

    struct Counters {
        quint64 updates = 0;
        quint64 rgb555Tiles = 0;
        quint64 atlasTiles = 0;
        quint64 cacheReferences = 0;
        quint64 cacheAdditions = 0;
        quint64 cacheEvictions = 0;
        quint64 scrollCopies = 0;
        /// Updates that carried a rectangle list without a pixel stream (or the
        /// other way round); the reference skips these, so does this port.
        quint64 skippedPixelPath = 0;
    };

    ScreenDecoder();

    void setOptions(const Options &options);
    const Options &options() const { return m_options; }

    /// Install the Zstandard dictionary (`8bit.dat`).  Required for the RGB555
    /// path; the JPEG/cache paths work without it.
    void setDictionary(const QByteArray &dictionary);
    bool hasDictionary() const { return m_zstd.hasDictionary(); }

    /// Allocate the canvas.  Called with the partner desktop size from the login
    /// acknowledgement (`desktopSizeFromLogin`).
    void resetCanvas(const QSize &size);

    /// `desktop_size_from_login`: Int32LE width/height in fields 1 and 2 of a
    /// kind-0 login acknowledgement.
    static QSize desktopSizeFromLogin(const QList<QByteArray> &fields);

    struct Result {
        int applied = 0;
        bool changed = false;
    };

    /// Apply one 11-field kind-1 update.  Throws UvException on any malformed or
    /// unsupported content; the canvas and cache are unchanged in that case.
    Result apply(const QList<QByteArray> &fields);

    /// The standalone RGB555 path (`apply_rgb555_update`), kept separate because
    /// the reference exposes and tests it independently.
    Result applyRgb555(const QList<QByteArray> &fields);

    FrameBuffer &frameBuffer() { return m_frameBuffer; }
    const FrameBuffer &frameBuffer() const { return m_frameBuffer; }
    TileCache &cache() { return m_tileCache; }
    const TileCache &cache() const { return m_tileCache; }
    ZstdDecoder &zstd() { return m_zstd; }
    const Counters &counters() const { return m_counters; }
    void resetCounters() { m_counters = Counters{}; }

private:
    /// One RGB555 rectangle after validation.
    struct PixelRect {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };
    struct CacheReference {
        int x = 0;
        int y = 0;
        qint32 key = 0;
    };
    struct CacheAddition {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        qint32 key = 0;
    };
    struct ScrollCopy {
        int delta = 0;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        QList<QRect> subRectangles;
    };

    /// Everything the validation phase produces and the apply phase consumes.
    struct Plan {
        std::optional<ScrollCopy> scroll;
        QList<CacheReference> references;
        QList<CacheAddition> additions;
        QList<PixelRect> pixelRectangles;
        QByteArray pixelData;
        QList<TilePlacement> atlasTiles;
        QImage atlas;
    };

    void validate(const QList<QByteArray> &fields, Plan *plan);
    int applyPlan(const Plan &plan);

    /// Blit `source` (parts of it) into the canvas checking every row range.
    void blit(const QImage &source, const QRect &sourceRect, const QPoint &target, bool allowOutOfBoundsBlack);

    FrameBuffer m_frameBuffer;
    TileCache m_tileCache;
    ZstdDecoder m_zstd;
    Options m_options;
    Counters m_counters;
};

} // namespace ultraview::decoder
