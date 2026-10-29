//
// Tests for the G.722 (64 kbit/s) decoder used for the partner's computer sound
// (command 43).
//
// The expected samples below were produced once with the ITU-T G.722 reference
// decoder as embedded in the GPL-2.0-or-later `remmina-plugin-ultraviewer`
// implementation (see NOTICE.md), compiled unmodified for the host, so this is a
// comparison against that reference rather than a self-consistency check.
//
#include "TestSupport.h"

#include "audio/G722Decoder.h"

#include <QtTest>

using namespace ultraview;
using namespace ultraview::audio;

namespace {

QVector<qint16> decodeBytes(const QByteArray &bytes, G722Decoder *decoder)
{
    QVector<qint16> samples;
    decoder->decode(reinterpret_cast<const quint8 *>(bytes.constData()), bytes.size(), &samples);
    return samples;
}

void compareSamples(const QVector<qint16> &actual, const QVector<qint16> &expected)
{
    QCOMPARE(actual.size(), expected.size());
    for (qsizetype index = 0; index < expected.size(); ++index) {
        if (actual.at(index) != expected.at(index)) {
            QFAIL(qPrintable(QStringLiteral("sample %1 is %2, expected %3")
                                 .arg(index)
                                 .arg(actual.at(index))
                                 .arg(expected.at(index))));
        }
    }
}

} // namespace

class TestG722 : public QObject {
    Q_OBJECT

private slots:
    void decodesSilenceVector();
    void decodesRampVector();
    void decodesFullScaleVector();
    void stateIsCarriedAcrossPackets();
    void resetRestoresTheInitialState();
    void emptyInputProducesNothing();
};

void TestG722::decodesSilenceVector()
{
    G722Decoder decoder;
    const QVector<qint16> expected = {
        0, 0, -1, -1, 0, 0, 0, -1, -1, 0, 0, -5, 2, -7, 2, -8,
        5, -13, 11, -19, 17, -23, 21, -30, 26, -33, 32, -44, 44, -52, 52, -66};
    compareSamples(decodeBytes(QByteArray(16, '\0'), &decoder), expected);
}

void TestG722::decodesRampVector()
{
    G722Decoder decoder;
    const QVector<qint16> expected = {
        0, 0, -1, -1, 0, -1, -1, 0, -1, -2, 1, -2, -3, -20, -4, 15,
        55, 28, -13, -132, -173, -153, 7, 151, 259, 146, -84, -375, -443, -266, 52, 320};
    compareSamples(decodeBytes(QByteArrayLiteral("\x00\x11\x22\x33\x44\x55\x66\x77\x88\x99\xaa\xbb\xcc\xdd\xee\xff"),
                               &decoder),
                   expected);
}

void TestG722::decodesFullScaleVector()
{
    G722Decoder decoder;
    const QVector<qint16> expected = {-1, 0, 0, -1, -1, 0, -1, -1,
                                      0, 0, -1, -2, -3, -3, -2, -2};
    compareSamples(decodeBytes(QByteArray(8, static_cast<char>(0xff)), &decoder), expected);
}

void TestG722::stateIsCarriedAcrossPackets()
{
    const QByteArray delta = QByteArrayLiteral("\x00\x11\x22\x33\x44\x55\x66\x77");
    const QByteArray continuation = QByteArrayLiteral("\x88\x99\xaa\xbb\xcc\xdd\xee\xff");

    G722Decoder whole;
    const QVector<qint16> once = decodeBytes(delta + continuation, &whole);

    G722Decoder split;
    QVector<qint16> twice = decodeBytes(delta, &split);
    twice += decodeBytes(continuation, &split);

    compareSamples(twice, once);
}

void TestG722::resetRestoresTheInitialState()
{
    G722Decoder warm;
    QVector<qint16> ignored;
    warm.decode(reinterpret_cast<const quint8 *>("\xff\xff\xff\xff"), 4, &ignored);
    warm.reset();

    const QVector<qint16> expected = {
        0, 0, -1, -1, 0, 0, 0, -1, -1, 0, 0, -5, 2, -7, 2, -8,
        5, -13, 11, -19, 17, -23, 21, -30, 26, -33, 32, -44, 44, -52, 52, -66};
    compareSamples(decodeBytes(QByteArray(16, '\0'), &warm), expected);
}

void TestG722::emptyInputProducesNothing()
{
    G722Decoder decoder;
    QVector<qint16> samples;
    samples.resize(4);
    decoder.decode(nullptr, 0, &samples);
    QVERIFY(samples.isEmpty());
    QVERIFY(decoder.decodeToPcm(QByteArray()).isEmpty());

    // One byte carries two 16-bit samples.
    const QByteArray pcm = decoder.decodeToPcm(QByteArray(3, '\0'));
    QCOMPARE(pcm.size(), 12);
}

QTEST_MAIN(TestG722)
#include "tst_g722.moc"
