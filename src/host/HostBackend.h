#pragma once
//
// Desktop capture and input injection boundary.
//
// Wayland is the primary backend (xdg-desktop-portal ScreenCast + RemoteDesktop
// with PipeWire).  A backend is only created when it can actually work; the UI
// receives an actionable error instead of a silently blank screen.
//
#include <QByteArray>
#include <QPoint>
#include <QString>
#include <QVector>

#include <memory>
#include <optional>

#include "host/HostProtocol.h"

namespace ultraview::host {

struct HostRect {
    qint32 x = 0;
    qint32 y = 0;
    qint32 width = 0;
    qint32 height = 0;

    bool isValid() const { return width > 0 && height > 0; }
    bool operator==(const HostRect &other) const = default;
};

/// Mapping between the transmitted canvas (always 0,0..width,height) and the
/// backend's desktop coordinates.
struct HostLayout {
    HostRect source;
    int width = 0;
    int height = 0;
    QVector<HostMonitor> monitors;
    QString id;

    bool isValid() const;
    QPoint mapToSource(int canvasX, int canvasY) const;
    /// Pick a capture budget and scale the union down without ever upscaling.
    static std::optional<HostLayout> build(const QVector<HostRect> &displays,
                                           QString *error = nullptr);
};

class HostBackend {
public:
    virtual ~HostBackend();

    virtual QString name() const = 0;
    virtual QString detail() const { return {}; }
    virtual bool start(QString *error) = 0;
    virtual void stop() = 0;

    /// Display rectangles in the backend's coordinate system.
    virtual QVector<HostRect> displays(QString *error) = 0;
    /// Capture the requested source rectangle as bottom-up RGB555.
    virtual QByteArray capture(const HostRect &source, QString *error) = 0;

    virtual void inject(const HostInputEvent &event, const HostLayout &layout) = 0;
    virtual void releaseAll() = 0;
    virtual bool isReady() const = 0;
};

/// Create the best backend for this session.  Returns null and fills `error`
/// when Wayland/X11 support is not compiled in or no display server is usable.
std::unique_ptr<HostBackend> createHostBackend(QString *error);

/// Capture one frame for `layout`, scaling it to the transmitted size.
std::optional<HostFrame> captureHostFrame(HostBackend &backend, const HostLayout &layout,
                                          QString *error);

} // namespace ultraview::host
