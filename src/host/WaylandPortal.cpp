#include "host/HostBackend.h"

#include "core/ErrorTypes.h"
#include "host/InputMapping.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QMutex>
#include <QMutexLocker>
#include <QPoint>
#include <QSet>
#include <QSize>
#include <QTimer>
#include <QVariantMap>
#include <QWaitCondition>
#include <QtEndian>

#include <pipewire/pipewire.h>
#include <spa/param/format-utils.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/pod/vararg.h>
#include <spa/utils/defs.h>

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

namespace ultraview::host {
namespace {

constexpr const char *kPortalService = "org.freedesktop.portal.Desktop";
constexpr const char *kPortalPath = "/org/freedesktop/portal/desktop";
constexpr const char *kRemoteDesktop = "org.freedesktop.portal.RemoteDesktop";
constexpr const char *kScreenCast = "org.freedesktop.portal.ScreenCast";
constexpr const char *kRequest = "org.freedesktop.portal.Request";
constexpr const char *kSession = "org.freedesktop.portal.Session";

constexpr qint64 kCaptureWaitMs = 5000;
constexpr int kPortalSetupTimeoutMs = 30000;
constexpr int kPortalStartTimeoutMs = 180000;

enum class PixelOrder { Bgrx, Rgbx, Xrgb, Xbgr, Rgb, Bgr };

int bytesPerPixel(PixelOrder order)
{
    return order == PixelOrder::Rgb || order == PixelOrder::Bgr ? 3 : 4;
}

void channelOffsets(PixelOrder order, int *red, int *green, int *blue)
{
    switch (order) {
    case PixelOrder::Bgrx:
    case PixelOrder::Bgr:
        *red = 2;
        *green = 1;
        *blue = 0;
        break;
    case PixelOrder::Rgbx:
    case PixelOrder::Rgb:
        *red = 0;
        *green = 1;
        *blue = 2;
        break;
    case PixelOrder::Xrgb:
        *red = 1;
        *green = 2;
        *blue = 3;
        break;
    case PixelOrder::Xbgr:
        *red = 3;
        *green = 2;
        *blue = 1;
        break;
    }
}

struct RawFrame {
    int width = 0;
    int height = 0;
    int stride = 0;
    PixelOrder order = PixelOrder::Bgrx;
    QByteArray data;
    bool hasFormat = false;
};

struct StreamState {
    quint32 node = 0;
    pw_stream *stream = nullptr;
    // PipeWire keeps a pointer to this table for the stream's whole lifetime;
    // it must not be a local in startPipeWire().
    pw_stream_events events{};
    spa_hook listener{};
    QMutex mutex;
    QWaitCondition condition;
    RawFrame frame;
    bool failed = false;
    QString error;
};

struct PortalStream {
    quint32 node = 0;
    // Logical (compositor) size reported by the portal; PipeWire frames arrive
    // at the physical buffer size.  Input coordinates use the logical space.
    std::optional<QSize> size;
    std::unique_ptr<StreamState> state;
};

QString objectPath(const QVariant &value)
{
    if (value.canConvert<QDBusObjectPath>())
        return value.value<QDBusObjectPath>().path();
    return value.toString();
}

QSize variantSize(const QVariant &value, bool *ok)
{
    *ok = false;
    if (value.metaType() == QMetaType::fromType<QSize>()) {
        *ok = true;
        return value.toSize();
    }
    QSize size;
    if (value.canConvert<QDBusArgument>()) {
        const QDBusArgument argument = value.value<QDBusArgument>();
        argument.beginStructure();
        int width = 0;
        int height = 0;
        argument >> width >> height;
        argument.endStructure();
        size = QSize(width, height);
        *ok = true;
    }
    return size;
}

QString portalFailure(const QDBusMessage &reply)
{
    if (reply.type() == QDBusMessage::ErrorMessage)
        return reply.errorMessage().left(256);
    return QStringLiteral("the desktop portal did not answer");
}

const spa_pod *buildVideoFormat(uint8_t *buffer, size_t size)
{
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(
        buffer, static_cast<uint32_t>(std::min<size_t>(size, UINT32_MAX)));
    spa_pod_frame frame{};
    spa_rectangle defaultSize = SPA_RECTANGLE(1920, 1080);
    spa_rectangle minimumSize = SPA_RECTANGLE(1, 1);
    spa_rectangle maximumSize = SPA_RECTANGLE(8192, 8192);
    spa_fraction defaultRate = SPA_FRACTION(30, 1);
    spa_fraction minimumRate = SPA_FRACTION(0, 1);
    spa_fraction maximumRate = SPA_FRACTION(120, 1);
    spa_pod_builder_push_object(&builder, &frame, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(
        &builder,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format,
        SPA_POD_CHOICE_ENUM_Id(10, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA,
                               SPA_VIDEO_FORMAT_RGBx, SPA_VIDEO_FORMAT_RGBA,
                               SPA_VIDEO_FORMAT_xRGB, SPA_VIDEO_FORMAT_ARGB,
                               SPA_VIDEO_FORMAT_xBGR, SPA_VIDEO_FORMAT_ABGR,
                               SPA_VIDEO_FORMAT_RGB, SPA_VIDEO_FORMAT_BGR),
        SPA_FORMAT_VIDEO_size,
        SPA_POD_CHOICE_RANGE_Rectangle(&defaultSize, &minimumSize, &maximumSize),
        SPA_FORMAT_VIDEO_framerate,
        SPA_POD_CHOICE_RANGE_Fraction(&defaultRate, &minimumRate, &maximumRate),
        0);
    return static_cast<const spa_pod *>(spa_pod_builder_pop(&builder, &frame));
}

std::optional<PixelOrder> pixelOrder(uint32_t format)
{
    switch (format) {
    case SPA_VIDEO_FORMAT_BGRx:
    case SPA_VIDEO_FORMAT_BGRA:
        return PixelOrder::Bgrx;
    case SPA_VIDEO_FORMAT_RGBx:
    case SPA_VIDEO_FORMAT_RGBA:
        return PixelOrder::Rgbx;
    case SPA_VIDEO_FORMAT_xRGB:
    case SPA_VIDEO_FORMAT_ARGB:
        return PixelOrder::Xrgb;
    case SPA_VIDEO_FORMAT_xBGR:
    case SPA_VIDEO_FORMAT_ABGR:
        return PixelOrder::Xbgr;
    case SPA_VIDEO_FORMAT_RGB:
        return PixelOrder::Rgb;
    case SPA_VIDEO_FORMAT_BGR:
        return PixelOrder::Bgr;
    default:
        return std::nullopt;
    }
}

void onStreamStateChanged(void *data, pw_stream_state, pw_stream_state state, const char *error)
{
    auto *stream = static_cast<StreamState *>(data);
    if (state != PW_STREAM_STATE_ERROR)
        return;
    QMutexLocker locker(&stream->mutex);
    stream->failed = true;
    stream->error = QString::fromUtf8(error != nullptr ? error : "PipeWire stream error");
    stream->condition.wakeAll();
}

void onStreamParamChanged(void *data, uint32_t id, const spa_pod *param)
{
    if (param == nullptr || id != SPA_PARAM_Format)
        return;
    auto *stream = static_cast<StreamState *>(data);
    struct spa_video_info info;
    spa_zero(info);
    if (spa_format_video_parse(param, &info) < 0
        || info.media_subtype != SPA_MEDIA_SUBTYPE_raw) {
        return;
    }
    const auto order = pixelOrder(info.info.raw.format);
    if (!order.has_value())
        return;
    QMutexLocker locker(&stream->mutex);
    stream->frame.width = static_cast<int>(info.info.raw.size.width);
    stream->frame.height = static_cast<int>(info.info.raw.size.height);
    stream->frame.order = *order;
    stream->frame.hasFormat = true;
}

void onStreamProcess(void *data)
{
    auto *stream = static_cast<StreamState *>(data);
    pw_buffer *buffer = pw_stream_dequeue_buffer(stream->stream);
    if (buffer == nullptr)
        return;
    spa_buffer *spaBuffer = buffer->buffer;
    if (spaBuffer == nullptr || spaBuffer->n_datas < 1) {
        pw_stream_queue_buffer(stream->stream, buffer);
        return;
    }
    spa_data &spaData = spaBuffer->datas[0];
    if (spaData.data == nullptr || spaData.chunk == nullptr) {
        pw_stream_queue_buffer(stream->stream, buffer);
        return;
    }
    const quint32 offset = spaData.chunk->offset;
    const quint32 length = spaData.chunk->size;
    const qint32 stride = spaData.chunk->stride;
    if (length == 0 || offset > spaData.maxsize || length > spaData.maxsize - offset) {
        pw_stream_queue_buffer(stream->stream, buffer);
        return;
    }
    QByteArray copy(reinterpret_cast<const char *>(spaData.data) + offset,
                    static_cast<qsizetype>(length));
    QMutexLocker locker(&stream->mutex);
    stream->frame.data = std::move(copy);
    stream->frame.stride = stride > 0 ? stride : 0;
    stream->condition.wakeAll();
    locker.unlock();
    pw_stream_queue_buffer(stream->stream, buffer);
}

} // namespace

class WaylandPortalBackend : public QObject, public HostBackend {
    Q_OBJECT

public:
    explicit WaylandPortalBackend(QObject *parent = nullptr);
    ~WaylandPortalBackend() override;

    QString name() const override { return QStringLiteral("Wayland portal (PipeWire)"); }
    QString detail() const override
    {
        return QStringLiteral("xdg-desktop-portal RemoteDesktop + ScreenCast");
    }
    bool start(QString *error) override;
    void stop() override;
    QVector<HostRect> displays(QString *error) override;
    QByteArray capture(const HostRect &source, QString *error) override;
    void inject(const HostInputEvent &event, const HostLayout &layout) override;
    void releaseAll() override;
    bool isReady() const override { return m_ready; }

private slots:
    void onPortalResponse(const QDBusMessage &message);

private:
    bool createSession(QString *error);
    bool selectDevices(QString *error);
    bool selectSources(QString *error);
    bool startRemoteDesktop(QString *error);
    bool openPipeWire(QString *error);
    bool startPipeWire(int fd, QString *error);
    bool request(const QString &interface, const QString &method, const QVariantList &args,
                 const QVariantMap &options, int timeoutMs, QVariantMap *results,
                 QString *error);
    QString newToken(const QString &prefix);
    void notify(const QString &method, const QVariantList &args);
    void closeSession();
    QVector<RawFrame> snapshot(QString *error);
    QVector<HostRect> streamRects(const QVector<RawFrame> &frames);
    QByteArray compose(const HostRect &source, const QVector<RawFrame> &frames);
    void sendMotion(quint32 node, double x, double y);
    void sendButton(qint32 code, bool pressed);
    void sendAxis(qint32 steps);
    void sendKey(const PortalKey &key, bool pressed);

    QDBusConnection m_bus;
    QString m_sessionPath;
    bool m_ready = false;
    bool m_pointerGranted = true;
    bool m_keyboardGranted = true;
    quint64 m_tokenCounter = 0;
    QEventLoop *m_waitLoop = nullptr;
    bool m_responseReady = false;
    uint m_responseCode = 2;
    QVariantMap m_responseResults;

    pw_thread_loop *m_loop = nullptr;
    pw_context *m_context = nullptr;
    pw_core *m_core = nullptr;
    std::vector<std::unique_ptr<PortalStream>> m_streams;

    quint32 m_lastStream = 0;
    qint32 m_wheelRemainder = 0;
    QVector<PortalKey> m_heldKeys;
    QSet<qint32> m_heldButtons;
};

WaylandPortalBackend::WaylandPortalBackend(QObject *parent)
    : QObject(parent)
    , m_bus(QDBusConnection::sessionBus())
{
}

WaylandPortalBackend::~WaylandPortalBackend()
{
    stop();
}

bool WaylandPortalBackend::start(QString *error)
{
    if (m_ready)
        return true;
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        if (error)
            *error = QStringLiteral("No Wayland session: WAYLAND_DISPLAY is not set.");
        return false;
    }
    m_bus = QDBusConnection::sessionBus();
    if (!m_bus.isConnected()) {
        if (error)
            *error = QStringLiteral("The D-Bus session bus is not available.");
        return false;
    }

    QString failure;
    if (!createSession(&failure) || !selectDevices(&failure) || !selectSources(&failure)
        || !startRemoteDesktop(&failure) || !openPipeWire(&failure)) {
        closeSession();
        if (error)
            *error = failure;
        return false;
    }
    m_ready = true;
    return true;
}

void WaylandPortalBackend::stop()
{
    releaseAll();
    if (m_loop != nullptr) {
        pw_thread_loop_stop(m_loop);
        pw_thread_loop_lock(m_loop);
        for (auto &stream : m_streams) {
            if (stream->state && stream->state->stream) {
                pw_stream_destroy(stream->state->stream);
                stream->state->stream = nullptr;
            }
        }
        m_streams.clear();
        if (m_core != nullptr) {
            pw_core_disconnect(m_core);
            m_core = nullptr;
        }
        if (m_context != nullptr) {
            pw_context_destroy(m_context);
            m_context = nullptr;
        }
        pw_thread_loop_unlock(m_loop);
        pw_thread_loop_destroy(m_loop);
        m_loop = nullptr;
    } else {
        m_streams.clear();
    }
    closeSession();
    m_ready = false;
}

QString WaylandPortalBackend::newToken(const QString &prefix)
{
    ++m_tokenCounter;
    return QStringLiteral("ultraview_%1_%2_%3")
        .arg(prefix)
        .arg(QCoreApplication::applicationPid())
        .arg(m_tokenCounter);
}

bool WaylandPortalBackend::request(const QString &interface, const QString &method,
                                   const QVariantList &args, const QVariantMap &options,
                                   int timeoutMs, QVariantMap *results, QString *error)
{
    const QString token = newToken(QStringLiteral("request"));
    QVariantMap requestOptions = options;
    requestOptions.insert(QStringLiteral("handle_token"), token);

    QString unique = m_bus.baseService();
    if (unique.startsWith(QLatin1Char(':')))
        unique = unique.mid(1);
    unique.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString predictedPath =
        QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(unique, token);

    m_responseReady = false;
    m_responseCode = 2;
    m_responseResults.clear();
    const bool connected = m_bus.connect(QString::fromLatin1(kPortalService), predictedPath,
                                         QString::fromLatin1(kRequest), QStringLiteral("Response"),
                                         this, SLOT(onPortalResponse(QDBusMessage)));
    if (!connected) {
        if (error)
            *error = QStringLiteral("Cannot listen for the desktop portal's response.");
        return false;
    }

    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kPortalService), QString::fromLatin1(kPortalPath), interface, method);
    for (const QVariant &argument : args)
        message << argument;
    message << requestOptions;
    const QDBusMessage reply = m_bus.call(message, QDBus::Block, kPortalSetupTimeoutMs);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_bus.disconnect(QString::fromLatin1(kPortalService), predictedPath,
                         QString::fromLatin1(kRequest), QStringLiteral("Response"), this,
                         SLOT(onPortalResponse(QDBusMessage)));
        if (error)
            *error = QStringLiteral("The desktop portal refused the request: %1")
                         .arg(portalFailure(reply));
        return false;
    }
    QString actualPath = objectPath(reply.arguments().value(0));
    if (actualPath.isEmpty())
        actualPath = predictedPath;
    if (actualPath != predictedPath) {
        m_bus.disconnect(QString::fromLatin1(kPortalService), predictedPath,
                         QString::fromLatin1(kRequest), QStringLiteral("Response"), this,
                         SLOT(onPortalResponse(QDBusMessage)));
        m_bus.connect(QString::fromLatin1(kPortalService), actualPath,
                      QString::fromLatin1(kRequest), QStringLiteral("Response"), this,
                      SLOT(onPortalResponse(QDBusMessage)));
    }

    if (!m_responseReady) {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        m_waitLoop = &loop;
        timer.start(qMax(1000, timeoutMs));
        loop.exec();
        m_waitLoop = nullptr;
    }
    m_bus.disconnect(QString::fromLatin1(kPortalService), actualPath,
                     QString::fromLatin1(kRequest), QStringLiteral("Response"), this,
                     SLOT(onPortalResponse(QDBusMessage)));
    if (!m_responseReady) {
        if (error)
            *error = QStringLiteral("The desktop portal did not answer in time.");
        return false;
    }
    if (m_responseCode != 0) {
        if (error) {
            *error = m_responseCode == 1
                ? QStringLiteral("The screen sharing permission request was cancelled.")
                : QStringLiteral("The desktop portal rejected the screen sharing request.");
        }
        return false;
    }
    if (results != nullptr)
        *results = m_responseResults;
    return true;
}

void WaylandPortalBackend::onPortalResponse(const QDBusMessage &message)
{
    if (message.arguments().size() < 2)
        return;
    m_responseCode = message.arguments().at(0).toUInt();
    m_responseResults = qdbus_cast<QVariantMap>(message.arguments().at(1));
    m_responseReady = true;
    if (m_waitLoop != nullptr)
        m_waitLoop->quit();
}

bool WaylandPortalBackend::createSession(QString *error)
{
    QVariantMap options;
    options.insert(QStringLiteral("session_handle_token"), newToken(QStringLiteral("session")));
    QVariantMap results;
    if (!request(QString::fromLatin1(kRemoteDesktop), QStringLiteral("CreateSession"), {},
                 options, kPortalSetupTimeoutMs, &results, error)) {
        return false;
    }
    m_sessionPath = objectPath(results.value(QStringLiteral("session_handle")));
    if (m_sessionPath.isEmpty()) {
        if (error)
            *error = QStringLiteral("The desktop portal returned no session handle.");
        return false;
    }
    return true;
}

bool WaylandPortalBackend::selectDevices(QString *error)
{
    QVariantMap options;
    options.insert(QStringLiteral("types"), QVariant::fromValue<quint32>(3)); // keyboard | pointer
    QVariantList args;
    args.append(QVariant::fromValue(QDBusObjectPath(m_sessionPath)));
    return request(QString::fromLatin1(kRemoteDesktop), QStringLiteral("SelectDevices"), args,
                   options, kPortalSetupTimeoutMs, nullptr, error);
}

bool WaylandPortalBackend::selectSources(QString *error)
{
    QVariantMap options;
    options.insert(QStringLiteral("types"), QVariant::fromValue<quint32>(1)); // monitor
    options.insert(QStringLiteral("multiple"), true);
    options.insert(QStringLiteral("cursor_mode"), QVariant::fromValue<quint32>(2)); // embedded
    QVariantList args;
    args.append(QVariant::fromValue(QDBusObjectPath(m_sessionPath)));
    return request(QString::fromLatin1(kScreenCast), QStringLiteral("SelectSources"), args,
                   options, kPortalSetupTimeoutMs, nullptr, error);
}

bool WaylandPortalBackend::startRemoteDesktop(QString *error)
{
    QVariantMap results;
    QVariantList args;
    args.append(QVariant::fromValue(QDBusObjectPath(m_sessionPath)));
    args.append(QString());
    // Start belongs to the RemoteDesktop interface: the portal stores the
    // returned stream list on the remote-desktop session, and only there does
    // OpenPipeWireRemote find it.  ScreenCast.SelectSources selected the monitors
    // for this same session handle.
    if (!request(QString::fromLatin1(kRemoteDesktop), QStringLiteral("Start"), args, {},
                 kPortalStartTimeoutMs, &results, error)) {
        return false;
    }

    const QVariant devices = results.value(QStringLiteral("devices"));
    if (devices.isValid()) {
        m_keyboardGranted = (devices.toUInt() & 1u) != 0;
        m_pointerGranted = (devices.toUInt() & 2u) != 0;
    }

    const QVariant streamsValue = results.value(QStringLiteral("streams"));
    if (!streamsValue.canConvert<QDBusArgument>()) {
        if (error)
            *error = QStringLiteral("The desktop portal returned no screen streams.");
        return false;
    }
    const QDBusArgument streams = streamsValue.value<QDBusArgument>();
    streams.beginArray();
    while (!streams.atEnd()) {
        streams.beginStructure();
        quint32 node = 0;
        QVariantMap properties;
        streams >> node >> properties;
        streams.endStructure();
        auto stream = std::make_unique<PortalStream>();
        stream->node = node;
        bool ok = false;
        stream->size = variantSize(properties.value(QStringLiteral("size")), &ok);
        if (!ok)
            stream->size.reset();
        auto state = std::make_unique<StreamState>();
        state->node = node;
        stream->state = std::move(state);
        m_streams.push_back(std::move(stream));
    }
    streams.endArray();
    if (m_streams.empty()) {
        if (error)
            *error = QStringLiteral("The desktop portal shared no monitor.");
        return false;
    }
    return true;
}

bool WaylandPortalBackend::openPipeWire(QString *error)
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kPortalService), QString::fromLatin1(kPortalPath),
        QString::fromLatin1(kScreenCast), QStringLiteral("OpenPipeWireRemote"));
    message << QVariant::fromValue(QDBusObjectPath(m_sessionPath)) << QVariantMap();
    const QDBusMessage reply = m_bus.call(message, QDBus::Block, kPortalSetupTimeoutMs);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error)
            *error = QStringLiteral("The desktop portal could not open PipeWire: %1")
                         .arg(portalFailure(reply));
        return false;
    }
    const QDBusUnixFileDescriptor descriptor =
        reply.arguments().value(0).value<QDBusUnixFileDescriptor>();
    if (!descriptor.isValid()) {
        if (error)
            *error = QStringLiteral("The desktop portal returned no PipeWire file descriptor.");
        return false;
    }
    const int duplicate = ::dup(descriptor.fileDescriptor());
    if (duplicate < 0) {
        if (error)
            *error = QStringLiteral("Cannot duplicate the PipeWire file descriptor.");
        return false;
    }
    return startPipeWire(duplicate, error);
}

bool WaylandPortalBackend::startPipeWire(int fd, QString *error)
{
    pw_init(nullptr, nullptr);
    m_loop = pw_thread_loop_new("ultraview-host", nullptr);
    if (m_loop == nullptr) {
        ::close(fd);
        if (error)
            *error = QStringLiteral("Cannot create the PipeWire thread loop.");
        return false;
    }
    pw_thread_loop_lock(m_loop);
    m_context = pw_context_new(pw_thread_loop_get_loop(m_loop), nullptr, 0);
    if (m_context == nullptr) {
        pw_thread_loop_unlock(m_loop);
        ::close(fd);
        if (error)
            *error = QStringLiteral("Cannot create the PipeWire context.");
        return false;
    }
    m_core = pw_context_connect_fd(m_context, fd, nullptr, 0);
    if (m_core == nullptr) {
        pw_thread_loop_unlock(m_loop);
        if (error)
            *error = QStringLiteral("Cannot connect to the portal's PipeWire server.");
        return false;
    }

    for (auto &portalStream : m_streams) {
        StreamState *state = portalStream->state.get();
        pw_properties *properties = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE,
            "Screen", nullptr);
        state->stream = pw_stream_new(m_core, "ultraview-host", properties);
        if (state->stream == nullptr) {
            pw_thread_loop_unlock(m_loop);
            if (error)
                *error = QStringLiteral("Cannot create a PipeWire screen stream.");
            return false;
        }
        state->events = {};
        state->events.version = PW_VERSION_STREAM_EVENTS;
        state->events.state_changed = onStreamStateChanged;
        state->events.param_changed = onStreamParamChanged;
        state->events.process = onStreamProcess;
        pw_stream_add_listener(state->stream, &state->listener, &state->events, state);

        uint8_t podBuffer[1024];
        const spa_pod *pod = buildVideoFormat(podBuffer, sizeof(podBuffer));
        const spa_pod *params[1] = {pod};
        const int result = pw_stream_connect(
            state->stream, PW_DIRECTION_INPUT, portalStream->node,
            static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT
                                         | PW_STREAM_FLAG_MAP_BUFFERS),
            params, 1);
        if (result < 0) {
            pw_thread_loop_unlock(m_loop);
            if (error)
                *error = QStringLiteral("Cannot connect the PipeWire screen stream.");
            return false;
        }
    }
    pw_thread_loop_unlock(m_loop);
    if (pw_thread_loop_start(m_loop) < 0) {
        if (error)
            *error = QStringLiteral("Cannot start the PipeWire thread loop.");
        return false;
    }
    return true;
}

QVector<RawFrame> WaylandPortalBackend::snapshot(QString *error)
{
    QVector<RawFrame> frames;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + kCaptureWaitMs;
    for (auto &portalStream : m_streams) {
        StreamState *state = portalStream->state.get();
        QMutexLocker locker(&state->mutex);
        while (!state->frame.hasFormat || state->frame.data.isEmpty()) {
            if (state->failed) {
                if (error)
                    *error = state->error;
                return {};
            }
            const qint64 remaining = deadline - QDateTime::currentMSecsSinceEpoch();
            if (remaining <= 0) {
                if (error)
                    *error = QStringLiteral("PipeWire delivered no screen frames.");
                return {};
            }
            state->condition.wait(&state->mutex, static_cast<unsigned long>(remaining));
        }
        frames.append(state->frame);
    }
    if (frames.isEmpty()) {
        if (error)
            *error = QStringLiteral("The portal shared no screen.");
    }
    return frames;
}

QVector<HostRect> WaylandPortalBackend::streamRects(const QVector<RawFrame> &frames)
{
    // The portal reports each stream's size in logical (compositor) pixels,
    // while PipeWire delivers frames at the physical buffer size.  The viewer
    // canvas and the composed capture use the physical pixels; pointer
    // coordinates are converted back to the logical stream space in inject().
    QVector<HostRect> rects;
    qint32 nextX = 0;
    for (size_t index = 0; index < m_streams.size()
         && static_cast<qsizetype>(index) < frames.size(); ++index) {
        const PortalStream &stream = *m_streams.at(index);
        const RawFrame &frame = frames.at(static_cast<qsizetype>(index));
        const int width = frame.width > 0
            ? frame.width
            : (stream.size.has_value() ? stream.size->width() : 0);
        const int height = frame.height > 0
            ? frame.height
            : (stream.size.has_value() ? stream.size->height() : 0);
        rects.append(HostRect{nextX, 0, width, height});
        if (width > 0)
            nextX += width;
    }
    return rects;
}

QByteArray WaylandPortalBackend::compose(const HostRect &source, const QVector<RawFrame> &frames)
{
    const QVector<HostRect> rects = streamRects(frames);
    if (source.width <= 0 || source.height <= 0)
        return {};
    QVector<quint16> topDown(static_cast<qsizetype>(source.width) * source.height, 0);
    for (qsizetype index = 0; index < rects.size() && index < frames.size(); ++index) {
        const HostRect &rect = rects.at(index);
        const RawFrame &frame = frames.at(index);
        if (!frame.hasFormat || frame.width <= 0 || frame.height <= 0 || rect.width <= 0
            || rect.height <= 0) {
            continue;
        }
        const int bytes = bytesPerPixel(frame.order);
        int red = 0;
        int green = 0;
        int blue = 0;
        channelOffsets(frame.order, &red, &green, &blue);
        const int stride = frame.stride > 0 ? frame.stride : frame.width * bytes;
        if (frame.data.size() < static_cast<qsizetype>(stride) * (frame.height - 1)
                + frame.width * bytes) {
            continue;
        }
        const qint32 left = std::max(source.x, rect.x);
        const qint32 top = std::max(source.y, rect.y);
        const qint32 right = std::min(source.x + source.width, rect.x + rect.width);
        const qint32 bottom = std::min(source.y + source.height, rect.y + rect.height);
        for (qint32 y = top; y < bottom; ++y) {
            const int sy = static_cast<int>(static_cast<qint64>(y - rect.y) * frame.height
                                            / rect.height);
            const char *row = frame.data.constData() + static_cast<qsizetype>(sy) * stride;
            for (qint32 x = left; x < right; ++x) {
                const int sx = static_cast<int>(static_cast<qint64>(x - rect.x) * frame.width
                                                / rect.width);
                const char *pixel = row + sx * bytes;
                const quint16 value = static_cast<quint16>(
                    ((static_cast<uchar>(pixel[red]) >> 3) << 10)
                    | ((static_cast<uchar>(pixel[green]) >> 3) << 5)
                    | (static_cast<uchar>(pixel[blue]) >> 3));
                topDown[(static_cast<qsizetype>(y) - source.y) * source.width
                        + (x - source.x)] = value;
            }
        }
    }
    QByteArray output;
    output.resize(static_cast<qsizetype>(source.width) * source.height * 2);
    char *target = output.data();
    for (int row = source.height - 1; row >= 0; --row) {
        const quint16 *line = topDown.constData() + static_cast<qsizetype>(row) * source.width;
        for (int column = 0; column < source.width; ++column) {
            const quint16 value = line[column];
            *target++ = static_cast<char>(value & 0xff);
            *target++ = static_cast<char>((value >> 8) & 0xff);
        }
    }
    return output;
}

QVector<HostRect> WaylandPortalBackend::displays(QString *error)
{
    if (!m_ready) {
        if (error)
            *error = QStringLiteral("The Wayland portal is not running.");
        return {};
    }
    const QVector<RawFrame> frames = snapshot(error);
    if (frames.isEmpty())
        return {};
    return streamRects(frames);
}

QByteArray WaylandPortalBackend::capture(const HostRect &source, QString *error)
{
    if (!m_ready) {
        if (error)
            *error = QStringLiteral("The Wayland portal is not running.");
        return {};
    }
    const QVector<RawFrame> frames = snapshot(error);
    if (frames.isEmpty())
        return {};
    return compose(source, frames);
}

void WaylandPortalBackend::notify(const QString &method, const QVariantList &args)
{
    if (!m_ready || m_sessionPath.isEmpty())
        return;
    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kPortalService), QString::fromLatin1(kPortalPath),
        QString::fromLatin1(kRemoteDesktop), method);
    for (const QVariant &argument : args)
        message << argument;
    m_bus.call(message, QDBus::NoBlock, 5000);
}

void WaylandPortalBackend::sendMotion(quint32 node, double x, double y)
{
    m_lastStream = node;
    // Every RemoteDesktop notify method takes (session, options, ...); the
    // options dictionary must not be omitted or shifted after the first value.
    notify(QStringLiteral("NotifyPointerMotionAbsolute"),
           {QVariant::fromValue(QDBusObjectPath(m_sessionPath)), QVariantMap(),
            QVariant::fromValue<quint32>(node), x, y});
}

void WaylandPortalBackend::sendButton(qint32 code, bool pressed)
{
    notify(QStringLiteral("NotifyPointerButton"),
           {QVariant::fromValue(QDBusObjectPath(m_sessionPath)), QVariantMap(), code,
            QVariant::fromValue<quint32>(pressed ? 1u : 0u)});
}

void WaylandPortalBackend::sendAxis(qint32 steps)
{
    notify(QStringLiteral("NotifyPointerAxisDiscrete"),
           {QVariant::fromValue(QDBusObjectPath(m_sessionPath)), QVariantMap(),
            QVariant::fromValue<quint32>(0), -steps});
}

void WaylandPortalBackend::sendKey(const PortalKey &key, bool pressed)
{
    const quint32 state = pressed ? 1u : 0u;
    if (key.kind == PortalKey::Kind::Keysym) {
        notify(QStringLiteral("NotifyKeyboardKeysym"),
               {QVariant::fromValue(QDBusObjectPath(m_sessionPath)), QVariantMap(), key.value,
                QVariant::fromValue<quint32>(state)});
    } else {
        notify(QStringLiteral("NotifyKeyboardKeycode"),
               {QVariant::fromValue(QDBusObjectPath(m_sessionPath)), QVariantMap(), key.value,
                QVariant::fromValue<quint32>(state)});
    }
}

void WaylandPortalBackend::inject(const HostInputEvent &event, const HostLayout &layout)
{
    if (!m_ready)
        return;

    if (event.type == HostInputType::Mouse) {
        if (!m_pointerGranted)
            return;
        const PointerAction action = pointerAction(event, &m_wheelRemainder);
        switch (action.kind) {
        case PointerAction::Kind::Move: {
            const QPoint source = layout.mapToSource(event.x, event.y);
            const QVector<HostRect> rects = streamRects(snapshot(nullptr));
            if (rects.isEmpty())
                return;
            qsizetype chosen = -1;
            for (qsizetype index = 0; index < rects.size(); ++index) {
                const HostRect &rect = rects.at(index);
                if (source.x() >= rect.x && source.x() < rect.x + rect.width
                    && source.y() >= rect.y && source.y() < rect.y + rect.height) {
                    chosen = index;
                    break;
                }
            }
            if (chosen < 0)
                chosen = 0;
            const HostRect &rect = rects.at(chosen);
            const PortalStream &stream = *m_streams.at(static_cast<size_t>(chosen));
            // The portal takes coordinates in the stream's logical space.  The
            // capture canvas and HostRect are in physical pixels; KWin applies
            // the output scale itself, so divide by the per-stream scale here.
            const double scale = stream.size.has_value() && stream.size->width() > 0
                    && rect.width > 0
                ? static_cast<double>(rect.width) / stream.size->width()
                : 1.0;
            const double logicalWidth = stream.size.has_value() && stream.size->width() > 0
                ? stream.size->width()
                : rect.width;
            const double logicalHeight = stream.size.has_value() && stream.size->height() > 0
                ? stream.size->height()
                : rect.height;
            const double x = std::clamp((source.x() - rect.x) / scale, 0.0,
                                        std::max(0.0, logicalWidth - 1));
            const double y = std::clamp((source.y() - rect.y) / scale, 0.0,
                                        std::max(0.0, logicalHeight - 1));
            sendMotion(stream.node, x, y);
            break;
        }
        case PointerAction::Kind::Button: {
            const qint32 code = action.button == 1 ? 0x110
                : action.button == 2                    ? 0x112
                                                        : 0x111;
            if (action.pressed) {
                if (!m_heldButtons.contains(code)) {
                    m_heldButtons.insert(code);
                    sendButton(code, true);
                }
            } else if (m_heldButtons.remove(code)) {
                sendButton(code, false);
            }
            break;
        }
        case PointerAction::Kind::Wheel:
            if (m_lastStream != 0)
                sendAxis(action.steps);
            break;
        case PointerAction::Kind::None:
            break;
        }
        return;
    }

    const bool down = event.kind == 1 || event.kind == 3;
    if (event.type == HostInputType::Keyboard && !m_keyboardGranted)
        return;
    const auto key = mapKey(event.kind, event.virtualKey, event.scanCode, event.character);
    if (!key.has_value())
        return;
    if (event.virtualKey == kVirtualKeyPacket) {
        if (down) {
            sendKey(*key, true);
            sendKey(*key, false);
        }
        return;
    }
    auto held = std::find_if(m_heldKeys.begin(), m_heldKeys.end(), [&key](const PortalKey &item) {
        return item.kind == key->kind && item.value == key->value;
    });
    if (down) {
        if (held == m_heldKeys.end()) {
            m_heldKeys.append(*key);
        }
        sendKey(*key, true);
    } else if (held != m_heldKeys.end()) {
        m_heldKeys.erase(held);
        sendKey(*key, false);
    }
}

void WaylandPortalBackend::releaseAll()
{
    for (auto it = m_heldKeys.crbegin(); it != m_heldKeys.crend(); ++it)
        sendKey(*it, false);
    m_heldKeys.clear();
    for (const qint32 button : std::as_const(m_heldButtons))
        sendButton(button, false);
    m_heldButtons.clear();
    m_wheelRemainder = 0;
    m_lastStream = 0;
}

void WaylandPortalBackend::closeSession()
{
    if (m_bus.isConnected() && !m_sessionPath.isEmpty()) {
        QDBusMessage message = QDBusMessage::createMethodCall(
            QString::fromLatin1(kPortalService), m_sessionPath,
            QString::fromLatin1(kSession), QStringLiteral("Close"));
        m_bus.call(message, QDBus::NoBlock, 5000);
    }
    m_sessionPath.clear();
}

std::unique_ptr<HostBackend> createWaylandPortalBackend(QString *error)
{
    Q_UNUSED(error);
    return std::make_unique<WaylandPortalBackend>();
}

} // namespace ultraview::host

#include "WaylandPortal.moc"
