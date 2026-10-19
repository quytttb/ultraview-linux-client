#pragma once
//
// JPEG atlas decoding (field 7 + field 8 of a kind-1 screen update).
//
// Recovered behaviour (screen_decode.py, ordinal 59/60 and the drawer worker):
//   * field 7 is a JPEG image whose rows are bottom-up (`TJFLAG_BOTTOMUP`);
//     the reference flips it with `ImageOps.flip(source.convert('RGB'))`,
//   * field 8 is a list of Int16 rectangles (x, y, width, height) describing which
//     part of the *partner screen* each atlas tile restores,
//   * `atlas_layout` then walks the rectangles in the observed tile order,
//     breaking the atlas into `block_size` columns and stacking tiles vertically
//     until the atlas height is exhausted, after which it starts a new column.
//
#include <QByteArray>
#include <QImage>
#include <QList>
#include <QRect>

namespace ultraview::decoder {

/// One tile placement computed by `atlas_layout`.
struct TilePlacement {
    int screenX = 0;
    int screenY = 0;
    int width = 0;
    int height = 0;
    int atlasX = 0;
    int atlasY = 0;
};

class JpegAtlasDecoder {
public:
    /// The reference refuses atlases above 32 * 1024 * 1024 pixels.
    static constexpr qint64 kMaxAtlasPixels = 32LL * 1024 * 1024;

    /// Decode a bottom-up JPEG atlas.  Throws ErrorCode::Protocol when the bytes
    /// are not a JPEG, ErrorCode::Resource when the atlas is too large.
    static QImage loadBottomUpAtlas(const QByteArray &jpegBytes);

    /// `atlas_layout`: (screen_x, screen_y, w, h, atlas_x, atlas_y) in the
    /// observed tile order.  Throws ErrorCode::Protocol when a tile cannot fit the
    /// atlas height and ErrorCode::Configuration for invalid geometry.
    static QList<TilePlacement> layout(const QList<QRect> &rectangles, int blockSize, int atlasHeight);

    /// Decode the Int16LE rectangle list of field 8.
    static QList<QRect> parseRectangles(const QByteArray &payload);
};

} // namespace ultraview::decoder
