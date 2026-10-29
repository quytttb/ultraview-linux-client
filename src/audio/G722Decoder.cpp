#include "audio/G722Decoder.h"

#include <algorithm>
#include <cstring>

namespace ultraview::audio {
namespace {

constexpr qint32 kQuantiserLow[8] = {-60, -30, 58, 172, 334, 538, 1198, 3042};
constexpr quint8 kReconstructLow[16] = {0, 7, 6, 5, 4, 3, 2, 1, 7, 6, 5, 4, 3, 2, 1, 0};
constexpr qint32 kInverseLog[32] = {
    2048, 2093, 2139, 2186, 2233, 2282, 2332, 2383, 2435, 2489, 2543, 2599,
    2656, 2714, 2774, 2834, 2896, 2960, 3025, 3091, 3158, 3228, 3298, 3371,
    3444, 3520, 3597, 3676, 3756, 3838, 3922, 4008};
constexpr qint32 kQuantiserHigh[3] = {0, -214, 798};
constexpr quint8 kReconstructHigh[4] = {2, 1, 2, 1};
constexpr qint32 kInverseQuantiser2[4] = {-7408, -1616, 7408, 1616};
constexpr qint32 kInverseQuantiser4[16] = {
    0, -20456, -12896, -8968, -6288, -4240, -2584, -1200,
    20456, 12896, 8968, 6288, 4240, 2584, 1200, 0};
constexpr qint32 kInverseQuantiser6[64] = {
    -136, -136, -136, -136, -24808, -21904, -19008, -16704, -14984, -13512,
    -12280, -11192, -10232, -9360, -8576, -7856, -7192, -6576, -6000, -5456,
    -4944, -4464, -4008, -3576, -3168, -2776, -2400, -2032, -1688, -1360,
    -1040, -728, 24808, 21904, 19008, 16704, 14984, 13512, 12280, 11192,
    10232, 9360, 8576, 7856, 7192, 6576, 6000, 5456, 4944, 4464, 4008, 3576,
    3168, 2776, 2400, 2032, 1688, 1360, 1040, 728, 432, 136, -432, -136};
constexpr qint32 kQmf[12] = {3, -11, 12, 32, -210, 951, 3876, -805, 362, -156, 53, -11};

inline qint32 saturate(qint32 value)
{
    return std::clamp(value, -32768, 32767);
}

inline qint32 clampTo(qint32 value, qint32 low, qint32 high)
{
    return std::clamp(value, low, high);
}

} // namespace

G722Decoder::G722Decoder()
{
    reset();
}

void G722Decoder::reset()
{
    std::memset(m_x, 0, sizeof(m_x));
    for (Band &band : m_band)
        band = Band{};
    m_band[0].det = 32;
    m_band[1].det = 8;
}

void G722Decoder::block4(Band *band, qint32 d)
{
    band->d[0] = d;
    band->r[0] = saturate(band->s + d);
    band->p[0] = saturate(band->sz + d);
    for (int index = 0; index < 3; ++index)
        band->sg[index] = band->p[index] >> 15;

    qint32 wd1 = saturate(band->a[1] * 4);
    qint32 wd2 = band->sg[0] == band->sg[1] ? -wd1 : wd1;
    if (wd2 > 32767)
        wd2 = 32767;
    qint32 wd3 = band->sg[0] == band->sg[2] ? 128 : -128;
    wd3 += wd2 >> 7;
    wd3 += (band->a[2] * 32512) >> 15;
    band->ap[2] = clampTo(wd3, -12288, 12288);

    band->sg[0] = band->p[0] >> 15;
    band->sg[1] = band->p[1] >> 15;
    wd1 = band->sg[0] == band->sg[1] ? 192 : -192;
    wd2 = (band->a[1] * 32640) >> 15;
    band->ap[1] = saturate(wd1 + wd2);
    wd3 = saturate(15360 - band->ap[2]);
    band->ap[1] = clampTo(band->ap[1], -wd3, wd3);

    wd1 = d == 0 ? 0 : 128;
    band->sg[0] = d >> 15;
    for (int index = 1; index < 7; ++index) {
        band->sg[index] = band->d[index] >> 15;
        wd2 = band->sg[index] == band->sg[0] ? wd1 : -wd1;
        wd3 = (band->b[index] * 32640) >> 15;
        band->bp[index] = saturate(wd2 + wd3);
    }

    for (int index = 6; index >= 1; --index) {
        band->d[index] = band->d[index - 1];
        band->b[index] = band->bp[index];
    }
    for (int index = 2; index >= 1; --index) {
        band->r[index] = band->r[index - 1];
        band->p[index] = band->p[index - 1];
        band->a[index] = band->ap[index];
    }

    wd1 = (band->a[1] * saturate(band->r[1] + band->r[1])) >> 15;
    wd2 = (band->a[2] * saturate(band->r[2] + band->r[2])) >> 15;
    band->sp = saturate(wd1 + wd2);
    qint32 sz = 0;
    for (int index = 6; index >= 1; --index)
        sz += (band->b[index] * saturate(band->d[index] + band->d[index])) >> 15;
    band->sz = saturate(sz);
    band->s = saturate(band->sp + band->sz);
}

void G722Decoder::decodeCode(quint8 code, qint16 *samples)
{
    quint32 wd1 = code & 0x3f;
    const quint32 ihigh = (code >> 6) & 3;

    // Lower sub-band: 6-bit inverse quantiser.
    qint32 wd2 = (m_band[0].det * kInverseQuantiser6[wd1]) >> 15;
    const qint32 rlow = clampTo(m_band[0].s + wd2, -16384, 16383);
    wd1 >>= 2;
    const qint32 dlowt = (m_band[0].det * kInverseQuantiser4[wd1]) >> 15;
    qint32 nb = clampTo(((m_band[0].nb * 127) >> 7) + kQuantiserLow[kReconstructLow[wd1]], 0,
                        18432);
    m_band[0].nb = nb;
    quint32 index = static_cast<quint32>((nb >> 6) & 31);
    int shift = 8 - (nb >> 11);
    m_band[0].det = (shift < 0 ? (kInverseLog[index] << -shift)
                               : (kInverseLog[index] >> shift))
        << 2;
    block4(&m_band[0], dlowt);

    // Upper sub-band: 2-bit inverse quantiser.
    const qint32 dhigh = (m_band[1].det * kInverseQuantiser2[ihigh]) >> 15;
    const qint32 rhigh = clampTo(dhigh + m_band[1].s, -16384, 16383);
    nb = clampTo(((m_band[1].nb * 127) >> 7) + kQuantiserHigh[kReconstructHigh[ihigh]], 0, 22528);
    m_band[1].nb = nb;
    index = static_cast<quint32>((nb >> 6) & 31);
    shift = 10 - (nb >> 11);
    m_band[1].det = (shift < 0 ? (kInverseLog[index] << -shift)
                               : (kInverseLog[index] >> shift))
        << 2;
    block4(&m_band[1], dhigh);

    // QMF synthesis: two output samples per input code.
    std::memmove(&m_x[0], &m_x[2], 22 * sizeof(qint32));
    m_x[22] = rlow + rhigh;
    m_x[23] = rlow - rhigh;
    qint64 xout1 = 0;
    qint64 xout2 = 0;
    for (int tap = 0; tap < 12; ++tap) {
        xout2 += static_cast<qint64>(m_x[2 * tap]) * kQmf[tap];
        xout1 += static_cast<qint64>(m_x[2 * tap + 1]) * kQmf[11 - tap];
    }
    samples[0] = static_cast<qint16>(xout1 >> 11);
    samples[1] = static_cast<qint16>(xout2 >> 11);
}

void G722Decoder::decode(const quint8 *input, qsizetype count, QVector<qint16> *output)
{
    if (output == nullptr)
        return;
    if (input == nullptr || count <= 0) {
        output->clear();
        return;
    }
    output->resize(count * 2);
    for (qsizetype index = 0; index < count; ++index)
        decodeCode(input[index], &(*output)[index * 2]);
}

QByteArray G722Decoder::decodeToPcm(const QByteArray &encoded)
{
    if (encoded.isEmpty())
        return QByteArray();
    QVector<qint16> samples;
    decode(reinterpret_cast<const quint8 *>(encoded.constData()), encoded.size(), &samples);
    QByteArray pcm(samples.size() * 2, Qt::Uninitialized);
    std::memcpy(pcm.data(), samples.constData(), static_cast<size_t>(pcm.size()));
    return pcm;
}

} // namespace ultraview::audio
