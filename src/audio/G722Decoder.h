#pragma once
//
// ITU-T G.722 sub-band ADPCM decoder (64 kbit/s, mode 1).
//
// The UltraViewer host streams Windows loopback audio with command 43; every
// byte carries one 8-bit code for a pair of 16-bit PCM samples at 16 kHz:
// two low-band bits in the two least significant bits are the 6-bit sub-band
// code split as `ihigh = code >> 6`, `ilow = code & 0x3f`.
//
// The algorithm below is the ITU-T G.722 64 kbit/s reference decoder (adaptive
// predictors in two bands, logarithmic quantiser adaptation and a 12-tap QMF
// synthesis filter).  It is a C++ port of the same public reference algorithm
// used by the GPL-2.0-or-later `remmina-plugin-ultraviewer` implementation, which
// this project credits in NOTICE.md.
//
// The decoder is stateful across packets on purpose: the adaptive predictors
// must keep their state for the stream to sound continuous.  One instance decodes
// one direction of one session.
//
#include <QByteArray>
#include <QVector>

#include <cstdint>

namespace ultraview::audio {

class G722Decoder {
public:
    static constexpr int kSampleRate = 16000;
    static constexpr int kChannels = 1;

    G722Decoder();

    /// Clear the predictor and QMF state (start of a stream / after a gap).
    void reset();

    /// Decode `count` bytes.  `output` is resized to `count * 2` samples and
    /// filled with signed 16-bit PCM.
    void decode(const quint8 *input, qsizetype count, QVector<qint16> *output);

    /// Convenience wrapper: decode `encoded` and return little-endian PCM bytes
    /// (what the audio sink consumes).
    QByteArray decodeToPcm(const QByteArray &encoded);

private:
    struct Band {
        qint32 s = 0;
        qint32 sp = 0;
        qint32 sz = 0;
        qint32 r[3] = {0, 0, 0};
        qint32 a[3] = {0, 0, 0};
        qint32 ap[3] = {0, 0, 0};
        qint32 p[3] = {0, 0, 0};
        qint32 d[7] = {0, 0, 0, 0, 0, 0, 0};
        qint32 b[7] = {0, 0, 0, 0, 0, 0, 0};
        qint32 bp[7] = {0, 0, 0, 0, 0, 0, 0};
        qint32 sg[7] = {0, 0, 0, 0, 0, 0, 0};
        qint32 nb = 0;
        qint32 det = 0;
    };

    void block4(Band *band, qint32 d);
    void decodeCode(quint8 code, qint16 *samples);

    qint32 m_x[24] = {0};
    Band m_band[2];
};

} // namespace ultraview::audio
