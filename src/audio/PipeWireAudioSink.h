#pragma once
//
// PipeWire playback sink for the remote computer sound stream.
//
// Only compiled when the PipeWire development headers are available
// (`ULTRAVIEW_HAVE_PIPEWIRE_AUDIO`).  The sink declares 16 kHz mono S16LE and lets
// the graph convert to the device rate/layout, exactly like the video capture
// path uses PipeWire rather than a private compositor protocol.
//
#include "audio/AudioPlayer.h"

namespace ultraview::audio {

class PipeWireAudioSink final : public AudioSink {
public:
    PipeWireAudioSink();
    ~PipeWireAudioSink() override;

    bool start(int sampleRate, int channels) override;
    void stop() override;
    void write(const QByteArray &pcm) override;
    bool isRunning() const override;
    QString description() const override;

    /// Called from the PipeWire thread; public so the C callback can reach it.
    void pullSamples(qint16 *destination, qsizetype frames, qsizetype channels);

private:
    struct Impl;
    Impl *m_impl = nullptr;
};

} // namespace ultraview::audio
