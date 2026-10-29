#pragma once
//
// Dictionary-free Zstandard helpers for the peer application messages.
//
// The screen pipeline (`decoder::ZstdDecoder`) always uses the vendor dictionary
// (`8bit.dat`).  Three other channels use plain Zstandard without a dictionary:
//
//   * clipboard, modern mode (command 33): the host sends compressed UTF-16LE,
//   * remote cursor shapes (command 4): the host sends compressed `.cur` data,
//   * clipboard upload (command 33): the viewer compresses UTF-16LE before it
//     prepends the ASCII length header.
//
// Everything here is bounded: a sender may not make this process allocate more
// than the caller's limit, and the declared frame size is checked before the
// allocation happens.
//
#include <QByteArray>

namespace ultraview::protocol {

class ZstdCodec {
public:
    /// Ceiling for any decompressed peer message.  Cursor shapes are a few KiB
    /// and clipboard text is capped by `kMaxClipboardBytes`, so 1 MiB is generous.
    static constexpr qint64 kDefaultMaxOutputBytes = 1024 * 1024;

    /// Compress `payload` (level 3).  Returns an empty array on failure; callers
    /// treat a failure as "do not send", never as an empty message.
    static QByteArray compress(const QByteArray &payload, int level = 3);

    /// Decompress exactly `expectedSize` bytes, or fail.
    /// Throws ErrorCode::Protocol for malformed input, ErrorCode::Resource when
    /// `expectedSize` exceeds `limit` or the frame declares more.
    static QByteArray decompress(const QByteArray &payload, qint64 expectedSize,
                                 qint64 limit = kDefaultMaxOutputBytes);

    /// Decompress a frame whose output size is declared by the frame itself.
    /// Throws when the frame does not declare its content size.  Named
    /// differently from the two-argument form above so a call cannot pick the
    /// wrong overload.
    static QByteArray decompressFrame(const QByteArray &payload,
                                      qint64 limit = kDefaultMaxOutputBytes);

    /// True when the payload starts with the zstd frame magic `28 b5 2f fd`.
    static bool hasFrameMagic(const QByteArray &payload);
};

} // namespace ultraview::protocol
