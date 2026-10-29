#include "audio/AudioPlayer.h"

#include <QCoreApplication>
#include "audio/G722Decoder.h"

#ifdef ULTRAVIEW_HAVE_PIPEWIRE_AUDIO
#include "audio/PipeWireAudioSink.h"
#endif

namespace ultraview::audio {
namespace {

/// A sink that accepts and discards everything.  Used when the build has no audio
/// backend; the session still decodes and counts packets, and the missing device
/// is reported once instead of silently swallowing the stream.
class NullAudioSink final : public AudioSink {
public:
    bool start(int sampleRate, int channels) override
    {
        m_running = sampleRate == G722Decoder::kSampleRate && channels == G722Decoder::kChannels;
        return m_running;
    }
    void stop() override { m_running = false; }
    void write(const QByteArray &) override {}
    bool isRunning() const override { return m_running; }
    QString description() const override
    {
        return QCoreApplication::translate("AudioSink", "no audio output (built without PipeWire)");
    }

private:
    bool m_running = false;
};

} // namespace

std::unique_ptr<AudioSink> makePlatformAudioSink()
{
#ifdef ULTRAVIEW_HAVE_PIPEWIRE_AUDIO
    return std::make_unique<PipeWireAudioSink>();
#else
    return std::make_unique<NullAudioSink>();
#endif
}

} // namespace ultraview::audio
