#include "platform/WaylandShortcutInhibitor.h"

#include "models/ConnectionViewModel.h"
#include "platform/wayland/KeyboardShortcutsInhibitClient.h"

#include <QGuiApplication>
#include <QTimer>
#include <QWindow>

#include <QtGui/qguiapplication_platform.h>
#include <qpa/qplatformwindow_p.h>

#include <algorithm>
#include <cstring>

namespace ultraview::platform {
struct WaylandShortcutInhibitor::Private {
    wl_display *display = nullptr;
    wl_seat *seat = nullptr;
    wl_registry *registry = nullptr;
    zwp_keyboard_shortcuts_inhibit_manager_v1 *manager = nullptr;
    zwp_keyboard_shortcuts_inhibitor_v1 *inhibitor = nullptr;
    wl_surface *surface = nullptr;
    bool registryRequested = false;
    bool active = false;
};

namespace {

using Manager = zwp_keyboard_shortcuts_inhibit_manager_v1;
using Inhibitor = zwp_keyboard_shortcuts_inhibitor_v1;
void registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface,
                    uint32_t version)
{
    auto *state = static_cast<WaylandShortcutInhibitor::Private *>(data);
    if (std::strcmp(interface, zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name) != 0)
        return;
    if (state->manager != nullptr)
        return;

    const uint32_t bindVersion = std::min(version, uint32_t{1});
    state->manager = static_cast<Manager *>(
        wl_registry_bind(registry, name, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, bindVersion));
}

void registryGlobalRemove(void *, wl_registry *, uint32_t)
{
}

constexpr wl_registry_listener kRegistryListener{registryGlobal, registryGlobalRemove};

void inhibitorActive(void *data, Inhibitor *)
{
    static_cast<WaylandShortcutInhibitor::Private *>(data)->active = true;
}

void inhibitorInactive(void *data, Inhibitor *)
{
    // Inactive is normal while the app does not own Wayland keyboard focus. The
    // compositor automatically activates the same request again on refocus.
    static_cast<WaylandShortcutInhibitor::Private *>(data)->active = false;
}

constexpr zwp_keyboard_shortcuts_inhibitor_v1_listener kInhibitorListener{inhibitorActive,
                                                                            inhibitorInactive};

} // namespace

WaylandShortcutInhibitor::WaylandShortcutInhibitor(models::ConnectionViewModel *controller,
                                                   QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_private(new Private)
{
    Q_ASSERT(m_controller != nullptr);
    connect(m_controller, &models::ConnectionViewModel::stateChanged, this,
            &WaylandShortcutInhibitor::update);
    connect(m_controller, &models::ConnectionViewModel::inputEnabledChanged, this,
            &WaylandShortcutInhibitor::update);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState) { update(); });
}

WaylandShortcutInhibitor::~WaylandShortcutInhibitor()
{
    releaseInhibitor();
    if (m_private->manager != nullptr)
        zwp_keyboard_shortcuts_inhibit_manager_v1_destroy(m_private->manager);
    if (m_private->registry != nullptr)
        wl_registry_destroy(m_private->registry);
    delete m_private;
}

void WaylandShortcutInhibitor::attach(QWindow *window)
{
    if (m_window == window)
        return;
    releaseInhibitor();
    m_window = window;
    if (m_window == nullptr)
        return;

    connect(m_window, &QWindow::activeChanged, this, &WaylandShortcutInhibitor::update);
    connect(m_window, &QWindow::visibleChanged, this,
            [this](bool) { update(); });
    connect(m_window, &QWindow::visibilityChanged, this,
            [this](QWindow::Visibility) { update(); });
    // QML can construct the ApplicationWindow before the Wayland surface exists.
    QTimer::singleShot(0, this, &WaylandShortcutInhibitor::update);
}

bool WaylandShortcutInhibitor::initializeProtocol()
{
    if (m_private->manager != nullptr)
        return true;
    if (m_private->registryRequested)
        return false;
    if (QGuiApplication::platformName() != QLatin1String("wayland") || m_window == nullptr)
        return false;

    const auto *waylandApplication =
        qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (waylandApplication == nullptr || waylandApplication->display() == nullptr
        || waylandApplication->seat() == nullptr) {
        return false;
    }

    m_private->display = waylandApplication->display();
    m_private->seat = waylandApplication->seat();
    m_private->registry = wl_display_get_registry(m_private->display);
    if (m_private->registry == nullptr)
        return false;

    m_private->registryRequested = true;
    wl_registry_add_listener(m_private->registry, &kRegistryListener, m_private);
    // The protocol registry is asynchronous. A one-time round trip discovers the
    // global before an active session needs to send a Super/Meta key.
    (void)wl_display_roundtrip(m_private->display);
    return m_private->manager != nullptr;
}

void WaylandShortcutInhibitor::releaseInhibitor()
{
    if (m_private == nullptr || m_private->inhibitor == nullptr)
        return;
    zwp_keyboard_shortcuts_inhibitor_v1_destroy(m_private->inhibitor);
    m_private->inhibitor = nullptr;
    m_private->surface = nullptr;
    m_private->active = false;
    if (m_private->display != nullptr)
        (void)wl_display_flush(m_private->display);
}

void WaylandShortcutInhibitor::update()
{
    const bool shouldInhibit = m_controller != nullptr && m_controller->isConnected()
        && m_controller->inputEnabled() && m_window != nullptr && m_window->isActive();
    if (!shouldInhibit) {
        releaseInhibitor();
        return;
    }
    if (!initializeProtocol())
        return;

    const auto *nativeWindow =
        m_window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
    wl_surface *surface = nativeWindow != nullptr ? nativeWindow->surface() : nullptr;
    if (surface == nullptr || m_private->seat == nullptr)
        return;
    if (m_private->inhibitor != nullptr && m_private->surface == surface)
        return;

    releaseInhibitor();
    m_private->surface = surface;
    m_private->inhibitor = zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
        m_private->manager, surface, m_private->seat);
    if (m_private->inhibitor == nullptr) {
        m_private->surface = nullptr;
        return;
    }
    zwp_keyboard_shortcuts_inhibitor_v1_add_listener(m_private->inhibitor, &kInhibitorListener,
                                                      m_private);
    (void)wl_display_flush(m_private->display);
}

} // namespace ultraview::platform
