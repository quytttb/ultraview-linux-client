#include "audio/PipeWireAudioSink.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/pod/builder.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace ultraview::audio {
namespace {

/// One second of mono audio: enough to ride out scheduling jitter in the graph
/// without adding audible latency (the callback only ever pulls one period).
constexpr qsizetype kRingSeconds = 1;

} // namespace

struct PipeWireAudioSink::Impl {
    pw_thread_loop *loop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_stream *stream = nullptr;
    spa_hook listener{};

    std::mutex mutex;
    std::vector<qint16> ring;
    qsizetype readIndex = 0;
    qsizetype count = 0;

    int rate = 0;
    int channels = 1;
    std::atomic<bool> running{false};
    std::atomic<bool> streaming{false};

    static void processThunk(void *data);
    static void stateThunk(void *data, pw_stream_state oldState, pw_stream_state state,
                           const char *error);
};

void PipeWireAudioSink::Impl::stateThunk(void *data, pw_stream_state, pw_stream_state state,
                                         const char *error)
{
    auto *impl = static_cast<Impl *>(data);
    if (impl == nullptr)
        return;
    impl->streaming = state == PW_STREAM_STATE_STREAMING;
    if (state == PW_STREAM_STATE_ERROR && error != nullptr)
        std::fprintf(stderr, "ultraview: audio stream error: %s\n", error);
}

void PipeWireAudioSink::Impl::processThunk(void *data)
{
    auto *impl = static_cast<Impl *>(data);
    if (impl == nullptr || impl->stream == nullptr)
        return;
    pw_buffer *buffer = pw_stream_dequeue_buffer(impl->stream);
    if (buffer == nullptr)
        return;
    spa_buffer *spaBuffer = buffer->buffer;
    if (spaBuffer == nullptr || spaBuffer->n_datas == 0) {
        pw_stream_queue_buffer(impl->stream, buffer);
        return;
    }
    spa_data &data0 = spaBuffer->datas[0];
    if (data0.data == nullptr) {
        pw_stream_queue_buffer(impl->stream, buffer);
        return;
    }
    const qsizetype frameBytes = static_cast<qsizetype>(sizeof(qint16)) * impl->channels;
    const qsizetype frames = frameBytes > 0 ? static_cast<qsizetype>(data0.maxsize) / frameBytes : 0;
    if (frames <= 0) {
        pw_stream_queue_buffer(impl->stream, buffer);
        return;
    }

    auto *destination = static_cast<qint16 *>(data0.data);
    // The realtime callback must never wait: if the producer is holding the lock,
    // this period is silence instead of a deadline miss.
    if (impl->mutex.try_lock()) {
        const qsizetype available = std::min(frames * impl->channels, impl->count);
        for (qsizetype index = 0; index < available; ++index) {
            destination[index] = impl->ring[static_cast<size_t>(impl->readIndex)];
            impl->readIndex = (impl->readIndex + 1) % static_cast<qsizetype>(impl->ring.size());
        }
        impl->count -= available;
        if (available < frames * impl->channels)
            std::memset(destination + available, 0, static_cast<size_t>(frames * frameBytes)
                                                       - static_cast<size_t>(available) * sizeof(qint16));
        impl->mutex.unlock();
    } else {
        std::memset(destination, 0, static_cast<size_t>(frames * frameBytes));
    }

    if (data0.chunk != nullptr) {
        data0.chunk->offset = 0;
        data0.chunk->stride = static_cast<int32_t>(frameBytes);
        data0.chunk->size = static_cast<uint32_t>(frames * frameBytes);
    }
    pw_stream_queue_buffer(impl->stream, buffer);
}

PipeWireAudioSink::PipeWireAudioSink()
    : m_impl(new Impl)
{
}

PipeWireAudioSink::~PipeWireAudioSink()
{
    stop();
    delete m_impl;
}

bool PipeWireAudioSink::start(int sampleRate, int channels)
{
    Impl *impl = m_impl;
    if (impl->running)
        return true;
    if (sampleRate <= 0 || channels <= 0 || channels > 8)
        return false;

    pw_init(nullptr, nullptr);
    impl->rate = sampleRate;
    impl->channels = channels;
    impl->ring.assign(static_cast<size_t>(sampleRate) * static_cast<size_t>(kRingSeconds), 0);
    impl->readIndex = 0;
    impl->count = 0;

    impl->loop = pw_thread_loop_new("ultraview-audio", nullptr);
    if (impl->loop == nullptr)
        return false;
    pw_thread_loop_lock(impl->loop);
    impl->context = pw_context_new(pw_thread_loop_get_loop(impl->loop), nullptr, 0);
    if (impl->context == nullptr) {
        pw_thread_loop_unlock(impl->loop);
        stop();
        return false;
    }
    impl->core = pw_context_connect(impl->context, nullptr, 0);
    if (impl->core == nullptr) {
        pw_thread_loop_unlock(impl->loop);
        stop();
        return false;
    }

    pw_properties *properties = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback", PW_KEY_MEDIA_ROLE, "Communication",
        PW_KEY_NODE_NAME, "ultraview-audio", PW_KEY_APP_NAME, "Ultraview", nullptr);
    impl->stream = pw_stream_new(impl->core, "ultraview-audio", properties);
    if (impl->stream == nullptr) {
        pw_thread_loop_unlock(impl->loop);
        stop();
        return false;
    }
    pw_stream_events events{};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.state_changed = &Impl::stateThunk;
    events.process = &Impl::processThunk;
    pw_stream_add_listener(impl->stream, &impl->listener, &events, impl);

    uint8_t podBuffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(podBuffer,
                                                  static_cast<uint32_t>(sizeof(podBuffer)));
    spa_pod_frame frame{};
    spa_pod_builder_push_object(&builder, &frame, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(&builder, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_audio),
                        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
                        SPA_FORMAT_AUDIO_format, SPA_POD_Id(SPA_AUDIO_FORMAT_S16),
                        SPA_FORMAT_AUDIO_rate, SPA_POD_Int(sampleRate),
                        SPA_FORMAT_AUDIO_channels, SPA_POD_Int(channels), 0);
    const spa_pod *params[1] = {
        static_cast<const spa_pod *>(spa_pod_builder_pop(&builder, &frame))};
    const int result = pw_stream_connect(
        impl->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS
                                     | PW_STREAM_FLAG_RT_PROCESS),
        params, 1);
    pw_thread_loop_unlock(impl->loop);
    if (result < 0) {
        stop();
        return false;
    }
    if (pw_thread_loop_start(impl->loop) != 0) {
        stop();
        return false;
    }
    impl->running = true;
    return true;
}

void PipeWireAudioSink::stop()
{
    Impl *impl = m_impl;
    impl->running = false;
    if (impl->loop != nullptr) {
        if (impl->stream != nullptr) {
            pw_thread_loop_lock(impl->loop);
            pw_stream_destroy(impl->stream);
            impl->stream = nullptr;
            pw_thread_loop_unlock(impl->loop);
        }
        pw_thread_loop_stop(impl->loop);
        if (impl->core != nullptr) {
            pw_thread_loop_lock(impl->loop);
            pw_core_disconnect(impl->core);
            impl->core = nullptr;
            if (impl->context != nullptr) {
                pw_context_destroy(impl->context);
                impl->context = nullptr;
            }
            pw_thread_loop_unlock(impl->loop);
        }
        pw_thread_loop_destroy(impl->loop);
        impl->loop = nullptr;
    }
    impl->streaming = false;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->count = 0;
        impl->readIndex = 0;
    }
}

void PipeWireAudioSink::write(const QByteArray &pcm)
{
    Impl *impl = m_impl;
    if (!impl->running || pcm.isEmpty() || impl->channels <= 0)
        return;
    const qsizetype sampleBytes = static_cast<qsizetype>(sizeof(qint16));
    const qsizetype samples = pcm.size() / sampleBytes;
    if (samples <= 0)
        return;
    const auto *source = reinterpret_cast<const qint16 *>(pcm.constData());
    std::lock_guard<std::mutex> lock(impl->mutex);
    const qsizetype capacity = static_cast<qsizetype>(impl->ring.size());
    for (qsizetype index = 0; index < samples; ++index) {
        if (impl->count == capacity) {
            // Drop the oldest sample: live audio is more useful than a backlog.
            impl->readIndex = (impl->readIndex + 1) % capacity;
            --impl->count;
        }
        impl->ring[static_cast<size_t>((impl->readIndex + impl->count) % capacity)] = source[index];
        ++impl->count;
    }
}

bool PipeWireAudioSink::isRunning() const
{
    return m_impl->running && m_impl->streaming;
}

QString PipeWireAudioSink::description() const
{
    return m_impl->running ? QStringLiteral("PipeWire") : QStringLiteral("PipeWire (not running)");
}

} // namespace ultraview::audio
