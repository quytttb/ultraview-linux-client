#include "audio/PipeWireAudioSink.h"

#include <QTest>

class PipeWireAudioTest : public QObject
{
    Q_OBJECT
private slots:
    void repeatedPlayback();
};

void PipeWireAudioTest::repeatedPlayback()
{
    if (!qEnvironmentVariableIsSet("UV_TEST_LIVE_PIPEWIRE"))
        QSKIP("Set UV_TEST_LIVE_PIPEWIRE=1 to exercise the local PipeWire server");

    ultraview::audio::PipeWireAudioSink sink;
    const QByteArray silence(640, '\0');
    for (int cycle = 0; cycle < 5; ++cycle) {
        QVERIFY(sink.start(16000, 1));
        QTRY_VERIFY_WITH_TIMEOUT(sink.isRunning(), 5000);
        // Return from start(), overwrite stack frames, and let the PipeWire
        // data thread repeatedly call its registered process callback.
        for (int i = 0; i < 30; ++i) {
            sink.write(silence);
            QTest::qWait(20);
        }
        sink.stop();
        QVERIFY(!sink.isRunning());
    }
}

QTEST_GUILESS_MAIN(PipeWireAudioTest)
#include "tst_pipewire_audio.moc"
