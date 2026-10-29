#pragma once
//
// Audio output for the remote computer sound stream (command 43).
//
// The pipeline is: relay payload -> G.722 decoder -> 16 kHz mono S16LE -> sink.
// A sink is chosen at compile time:
//
//   * PipeWire (`ULTRAVIEW_HAVE_PIPEWIRE_AUDIO`) when the development headers are
//     present; the graph converts the 16 kHz mono stream to the device rate, so
//     this client never resamples or upmixes by hand,
//   * a null sink otherwise, which keeps the protocol path testable and makes the
//     missing device an explicit notice instead of silent loss.
//
// `AudioPlayer` is the object the session talks to.  It lives in the GUI thread,
// owns the decoder state (which must survive across packets) and never blocks:
// the sink buffers, and a stalled device drops audio instead of stalling the
// session worker.
//
#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>

namespace ultraview::audio {

class AudioSink {
public:
    virtual ~AudioSink() = default;

    /// Open the device for signed 16-bit little-endian PCM.
    virtual bool start(int sampleRate, int channels) = 0;
    virtual void stop() = 0;
    /// Enqueue PCM; must not block.  The implementation may drop when full.
    virtual void write(const QByteArray &pcm) = 0;
    virtual bool isRunning() const = 0;
    /// Short, user-visible description of the backend.
    virtual QString description() const = 0;
};

/// The best sink this build can offer; never null.
std::unique_ptr<AudioSink> makePlatformAudioSink();

class AudioPlayer : public QObject {
    Q_OBJECT

public:
    explicit AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer() override;

    /// True when the build has a real backend (PipeWire) and it started.
    bool hasOutput() const;
    bool isEnabled() const { return m_enabled; }
    QString outputDescription() const;

    /// Enable/disable playback.  Emits `streamRequested` so the session can send
    /// the documented command 46 toggle; the sink is opened on the first packet
    /// so an unused session never claims an audio device.
    void setEnabled(bool enabled);

    quint64 packets() const { return m_packets; }
    quint64 silentPackets() const { return m_silentPackets; }
    quint64 decodedBytes() const { return m_decodedBytes; }
    /// Packets dropped because playback is disabled or the device stalled.
    quint64 droppedPackets() const { return m_droppedPackets; }

public slots:
    /// Command 43: raw G.722 bytes.
    void submitEncoded(const QByteArray &encoded);
    /// Command 42: the host asks for `pcmBytes` of silence.
    void submitSilence(int pcmBytes);
    /// The session ended; close the device and reset the decoder.
    void reset();

signals:
    /// `46|||-1` / `46|||0` must be sent to the partner.
    void streamRequested(bool enabled);
    /// One-shot, credential-free report for the session log.
    void notice(const QString &message);

private:
    bool ensureStarted();

    std::unique_ptr<AudioSink> m_sink;
    struct G722State;
    std::unique_ptr<G722State> m_decoder;
    bool m_enabled = false;
    bool m_startAttempted = false;
    bool m_noticeSent = false;
    quint64 m_packets = 0;
    quint64 m_silentPackets = 0;
    quint64 m_decodedBytes = 0;
    quint64 m_droppedPackets = 0;
};

} // namespace ultraview::audio
