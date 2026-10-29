#include "audio/AudioPlayer.h"

#include "audio/G722Decoder.h"
#include "core/ErrorTypes.h"

#include <algorithm>

namespace ultraview::audio {

struct AudioPlayer::G722State {
    G722Decoder decoder;
};

AudioPlayer::AudioPlayer(QObject *parent)
    : QObject(parent)
{
}

AudioPlayer::~AudioPlayer() = default;

QString AudioPlayer::outputDescription() const
{
    return m_sink != nullptr ? m_sink->description() : QStringLiteral("no audio output");
}

bool AudioPlayer::hasOutput() const
{
    return m_sink != nullptr && m_sink->isRunning();
}

void AudioPlayer::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    if (enabled) {
        // The device is opened lazily on the first packet: a session where the
        // partner sends no audio must not occupy the sound card.
        m_startAttempted = false;
        m_noticeSent = false;
    } else if (m_sink != nullptr) {
        m_sink->stop();
    }
    emit streamRequested(enabled);
}

bool AudioPlayer::ensureStarted()
{
    if (m_sink != nullptr && m_sink->isRunning())
        return true;
    if (m_startAttempted)
        return m_sink != nullptr && m_sink->isRunning();
    m_startAttempted = true;

    if (m_sink == nullptr)
        m_sink = makePlatformAudioSink();
    if (m_sink == nullptr)
        return false;

    const bool started = m_sink->start(G722Decoder::kSampleRate, G722Decoder::kChannels);
    if (!started && !m_noticeSent) {
        m_noticeSent = true;
        emit notice(QStringLiteral("the partner streams audio but no audio output is available "
                                   "(%1); the session continues without sound")
                        .arg(m_sink->description()));
    }
    return started;
}

void AudioPlayer::submitEncoded(const QByteArray &encoded)
{
    if (!m_enabled) {
        ++m_droppedPackets;
        return;
    }
    if (encoded.isEmpty())
        return;
    // One relay packet is a fraction of a second of audio; anything larger than
    // a second is a framing error, not audio.
    if (encoded.size() > G722Decoder::kSampleRate) {
        ++m_droppedPackets;
        return;
    }
    if (!ensureStarted()) {
        ++m_droppedPackets;
        return;
    }
    if (m_decoder == nullptr)
        m_decoder = std::make_unique<G722State>();

    const QByteArray pcm = m_decoder->decoder.decodeToPcm(encoded);
    if (pcm.isEmpty())
        return;
    ++m_packets;
    m_decodedBytes += static_cast<quint64>(pcm.size());
    m_sink->write(pcm);
}

void AudioPlayer::submitSilence(int pcmBytes)
{
    if (!m_enabled || pcmBytes <= 0) {
        ++m_droppedPackets;
        return;
    }
    if (!ensureStarted()) {
        ++m_droppedPackets;
        return;
    }
    // The host sends the silence length instead of encoded silence to avoid
    // transmitting it; the sink still needs real zero samples to stay fed.
    const int bounded = std::min(pcmBytes, G722Decoder::kSampleRate * 2);
    QByteArray silence(bounded, '\0');
    ++m_silentPackets;
    m_sink->write(silence);
}

void AudioPlayer::reset()
{
    if (m_sink != nullptr)
        m_sink->stop();
    if (m_decoder != nullptr)
        m_decoder->decoder.reset();
    m_startAttempted = false;
    m_noticeSent = false;
}

} // namespace ultraview::audio
