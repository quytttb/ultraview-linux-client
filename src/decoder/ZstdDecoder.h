#pragma once
//
// Bounded Zstandard decoding with the recovered screen dictionary.
//
// The reference (`screen_decode.ZstdDictionaryDecoder`) decompresses
// `ZSTD_decompress_usingDict` into a caller-sized buffer and requires the result
// length to match exactly.  The native decoder keeps that contract and adds the
// bounds the reference leaves to `max_output`:
//
//   * the dictionary is loaded once per context (`ZSTD_DCtx_loadDictionary`)
//     instead of being re-parsed for every rectangle group,
//   * the declared content size of the frame is checked *before* allocating, so a
//     decompression bomb is refused up front,
//   * the produced size must equal the expected size exactly.
//
// The class is **not** thread safe: one instance per decode thread.
//
#include <QByteArray>

#include <cstdint>

struct ZSTD_DCtx_s;
typedef struct ZSTD_DCtx_s ZSTD_DCtx;

namespace ultraview::decoder {

class ZstdDecoder {
public:
    /// The reference caps decoded images at 64 MiB (`max_output=64*1024*1024`).
    static constexpr qint64 kDefaultMaxOutputBytes = 64 * 1024 * 1024;

    ZstdDecoder();
    ~ZstdDecoder();
    ZstdDecoder(const ZstdDecoder &) = delete;
    ZstdDecoder &operator=(const ZstdDecoder &) = delete;

    /// Install the dictionary (the `8bit.dat` artifact).  An empty dictionary is
    /// accepted; the dictionary is copied, so the caller keeps ownership.
    /// Throws ErrorCode::Configuration when zstd rejects the dictionary.
    void setDictionary(const QByteArray &dictionary);

    const QByteArray &dictionary() const { return m_dictionary; }
    bool hasDictionary() const { return !m_dictionary.isEmpty(); }

    void setMaxOutputBytes(qint64 limit);
    qint64 maxOutputBytes() const { return m_maxOutputBytes; }

    /// Decompress `payload` and require exactly `expectedSize` bytes.
    /// Throws ErrorCode::Protocol on a malformed frame or a size mismatch and
    /// ErrorCode::Resource when a bound would be exceeded.
    QByteArray decompress(const QByteArray &payload, qint64 expectedSize);

    /// Content size declared by the frame header, or -1 when unknown.
    static qint64 declaredContentSize(const QByteArray &payload);

    /// True when the payload starts with the zstd frame magic; the reference uses
    /// this literal check (`fields[2].startswith(b'\x28\xb5\x2f\xfd')`).
    static bool hasFrameMagic(const QByteArray &payload);

private:
    ZSTD_DCtx *m_context = nullptr;
    QByteArray m_dictionary;
    qint64 m_maxOutputBytes = kDefaultMaxOutputBytes;
    bool m_dictionaryLoaded = false;
};

} // namespace ultraview::decoder
