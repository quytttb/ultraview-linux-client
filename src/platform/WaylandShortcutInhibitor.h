#pragma once

// Wayland/KWin keyboard-shortcut inhibition for an active remote-control
// session. The compositor then delivers Super/Meta and other desktop shortcut
// keys to this window instead of acting on them locally, so they can be sent to
// the Windows partner. It is intentionally a no-op off Wayland or when the
// compositor does not advertise the standard unstable-v1 protocol.

#include <QObject>

class QWindow;

namespace ultraview::models {
class ConnectionViewModel;
}

namespace ultraview::platform {

class WaylandShortcutInhibitor final : public QObject {
public:
    // Kept public solely so the C Wayland protocol callbacks in the .cpp can
    // carry an opaque pointer to it; callers never need to name or inspect it.
    struct Private;

    explicit WaylandShortcutInhibitor(models::ConnectionViewModel *controller,
                                      QObject *parent = nullptr);
    ~WaylandShortcutInhibitor() override;

    /// Begin following the lifetime/activity of @p window. Safe before the
    /// Wayland surface exists; inhibition is requested once it does.
    void attach(QWindow *window);

private:
    void update();
    bool initializeProtocol();
    void releaseInhibitor();

    models::ConnectionViewModel *m_controller = nullptr;
    QWindow *m_window = nullptr;
    Private *m_private = nullptr;
};

} // namespace ultraview::platform
