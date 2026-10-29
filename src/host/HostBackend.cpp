#include "host/HostBackend.h"

#include "core/ErrorTypes.h"

#include <QProcessEnvironment>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ultraview::host {

#if defined(ULTRAVIEW_HAVE_WAYLAND_HOST)
std::unique_ptr<HostBackend> createWaylandPortalBackend(QString *error);
#endif

HostBackend::~HostBackend() = default;

bool HostLayout::isValid() const
{
    if (!source.isValid() || width <= 0 || height <= 0 || width > kMaxDimension
        || height > kMaxDimension) {
        return false;
    }
    for (const HostMonitor &monitor : monitors) {
        if (monitor.left < 0 || monitor.top < 0 || monitor.right <= monitor.left
            || monitor.bottom <= monitor.top || monitor.right > width
            || monitor.bottom > height) {
            return false;
        }
    }
    return true;
}

QPoint HostLayout::mapToSource(int canvasX, int canvasY) const
{
    if (!isValid())
        return {};
    canvasX = std::clamp(canvasX, 0, width - 1);
    canvasY = std::clamp(canvasY, 0, height - 1);
    const qint64 x = source.x + static_cast<qint64>(canvasX) * source.width / width;
    const qint64 y = source.y + static_cast<qint64>(canvasY) * source.height / height;
    return QPoint(static_cast<int>(std::clamp<qint64>(x, source.x,
                                                      source.x + source.width - 1)),
                  static_cast<int>(std::clamp<qint64>(y, source.y,
                                                      source.y + source.height - 1)));
}

std::optional<HostLayout> HostLayout::build(const QVector<HostRect> &displays, QString *error)
{
    QVector<HostRect> valid;
    for (const HostRect &display : displays) {
        if (!display.isValid() || display.width > 65535 || display.height > 65535)
            continue;
        valid.append(display);
    }
    if (valid.isEmpty()) {
        if (error)
            *error = QStringLiteral("The desktop did not report a usable display.");
        return std::nullopt;
    }
    if (valid.size() > 64) {
        if (error)
            *error = QStringLiteral("Too many displays to share.");
        return std::nullopt;
    }

    qint64 left = valid.first().x;
    qint64 top = valid.first().y;
    qint64 right = static_cast<qint64>(valid.first().x) + valid.first().width;
    qint64 bottom = static_cast<qint64>(valid.first().y) + valid.first().height;
    for (const HostRect &display : valid) {
        left = std::min<qint64>(left, display.x);
        top = std::min<qint64>(top, display.y);
        right = std::max<qint64>(right, static_cast<qint64>(display.x) + display.width);
        bottom = std::max<qint64>(bottom, static_cast<qint64>(display.y) + display.height);
    }
    if (left < std::numeric_limits<qint32>::min() || top < std::numeric_limits<qint32>::min()
        || right > std::numeric_limits<qint32>::max()
        || bottom > std::numeric_limits<qint32>::max()) {
        if (error)
            *error = QStringLiteral("The desktop geometry is outside the supported range.");
        return std::nullopt;
    }

    const HostRect source{static_cast<qint32>(left), static_cast<qint32>(top),
                          static_cast<qint32>(right - left), static_cast<qint32>(bottom - top)};
    const double budget = valid.size() > 1 ? 8294400.0 : 2073600.0;
    double scale = std::min(1.0, std::sqrt(budget
                                           / (static_cast<double>(source.width) * source.height)));
    scale = std::min(scale, static_cast<double>(kMaxDimension) / source.width);
    scale = std::min(scale, static_cast<double>(kMaxDimension) / source.height);
    if (!(scale > 0.0))
        scale = 1.0;
    int width = std::max(1, static_cast<int>(std::floor(source.width * scale)));
    int height = std::max(1, static_cast<int>(std::floor(source.height * scale)));
    width = std::min(width, kMaxDimension);
    height = std::min(height, kMaxDimension);

    HostLayout layout;
    layout.source = source;
    layout.width = width;
    layout.height = height;
    const auto scaledX = [&](qint64 x) {
        return static_cast<qint32>(std::clamp<qint64>(
            static_cast<qint64>(std::floor((x - source.x) * width / source.width)), 0, width));
    };
    const auto scaledY = [&](qint64 y) {
        return static_cast<qint32>(std::clamp<qint64>(
            static_cast<qint64>(std::floor((y - source.y) * height / source.height)), 0, height));
    };
    if (valid.size() > 1) {
        for (const HostRect &display : valid) {
            HostMonitor monitor;
            monitor.left = scaledX(display.x);
            monitor.top = scaledY(display.y);
            monitor.right = scaledX(static_cast<qint64>(display.x) + display.width);
            monitor.bottom = scaledY(static_cast<qint64>(display.y) + display.height);
            if (monitor.right <= monitor.left)
                monitor.right = std::min(width, monitor.left + 1);
            if (monitor.bottom <= monitor.top)
                monitor.bottom = std::min(height, monitor.top + 1);
            layout.monitors.append(monitor);
        }
    }

    QString id = QStringLiteral("%1,%2,%3x%4").arg(source.x).arg(source.y).arg(width).arg(height);
    for (const HostRect &display : valid) {
        id += QStringLiteral(";%1,%2,%3x%4")
                  .arg(display.x)
                  .arg(display.y)
                  .arg(display.width)
                  .arg(display.height);
    }
    layout.id = id;
    if (!layout.isValid()) {
        if (error)
            *error = QStringLiteral("The desktop layout cannot be represented in the "
                                    "UltraViewer canvas.");
        return std::nullopt;
    }
    return layout;
}

std::optional<HostFrame> captureHostFrame(HostBackend &backend, const HostLayout &layout,
                                          QString *error)
{
    if (!layout.isValid()) {
        if (error)
            *error = QStringLiteral("The host screen layout is invalid.");
        return std::nullopt;
    }
    const QByteArray captured = backend.capture(layout.source, error);
    const qsizetype expected = static_cast<qsizetype>(layout.source.width)
        * layout.source.height * 2;
    if (captured.size() != expected) {
        if (error)
            *error = QStringLiteral("The captured screen has an unexpected size.");
        return std::nullopt;
    }
    HostFrame frame;
    frame.width = layout.width;
    frame.height = layout.height;
    frame.monitors = layout.monitors;
    frame.layoutId = layout.id;
    if (captured.size() == static_cast<qsizetype>(layout.width) * layout.height * 2) {
        frame.rgb555BottomUp = captured;
    } else {
        try {
            frame.rgb555BottomUp = scaleRgb555(captured, layout.source.width,
                                               layout.source.height, layout.width, layout.height);
        } catch (const UvException &exception) {
            if (error)
                *error = exception.message();
            return std::nullopt;
        }
    }
    if (!frame.isValid()) {
        if (error)
            *error = QStringLiteral("The captured screen frame is invalid.");
        return std::nullopt;
    }
    return frame;
}

std::unique_ptr<HostBackend> createHostBackend(QString *error)
{
    const QString forced = qEnvironmentVariable("ULTRAVIEW_HOST_BACKEND").trimmed().toLower();
#if defined(ULTRAVIEW_HAVE_WAYLAND_HOST)
    if (forced.isEmpty() || forced == QLatin1String("wayland")
        || forced == QLatin1String("portal")) {
        return createWaylandPortalBackend(error);
    }
#endif
    if (forced == QLatin1String("x11")) {
        if (error)
            *error = QStringLiteral("The X11 host backend is not available in this build.");
        return nullptr;
    }
    if (error)
        *error = QStringLiteral("Host screen capture is unavailable. A Wayland session with "
                                "xdg-desktop-portal and PipeWire is required by this build.");
    return nullptr;
}

} // namespace ultraview::host
