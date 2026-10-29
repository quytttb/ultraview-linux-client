#include "platform/GlobalMenu.h"

#include "models/ConnectionViewModel.h"

#include <QCoreApplication>
#include <QIcon>
#include <QWindow>

#include <utility>

#if defined(ULTRAVIEW_HAVE_GLOBAL_MENU)
// Private QtGui API: the platform theme hands out the menu bar implementation
// for this desktop.  See the class comment in the header for why this is the
// supported route for a Qt Quick application.
#  include <QtGui/private/qdesktopunixservices_p.h>
#  include <QtGui/private/qguiapplication_p.h>
#  include <qpa/qplatformintegration.h>
#  include <qpa/qplatformmenu.h>
#  include <qpa/qplatformtheme.h>
#endif

namespace ultraview::platform {

#if defined(ULTRAVIEW_HAVE_GLOBAL_MENU)
namespace {

// Qt's D-Bus menu bar announces the window to the desktop through the platform
// services of X11/Wayland (a window property, or the `org_kde_kwin_appmenu`
// Wayland protocol) and dereferences them without checking.  A platform whose
// services are not those (the offscreen plugin, for example) would crash on
// registration, so a menu bar that cannot be announced is refused here and the
// application simply has no global menu.
bool canAnnounceMenuBar()
{
    const QPlatformIntegration *integration = QGuiApplicationPrivate::platformIntegration();
    if (integration == nullptr)
        return false;
    return dynamic_cast<QDesktopUnixServices *>(integration->services()) != nullptr;
}

} // namespace
#endif

QVector<MenuItemDescription> describeMenu(const MenuState &state)
{
    QVector<MenuItemDescription> entries;

    // -------------------------------------------------------------- Session
    entries.append({.id = QStringLiteral("session"), .text = QCoreApplication::translate("GlobalMenu", "Session"), .isMenu = true});
    entries.append({.id = QStringLiteral("session.connect"),
                    .parentId = QStringLiteral("session"),
                    .text = QCoreApplication::translate("GlobalMenu", "Connect"),
                    .iconName = QStringLiteral("network-connect"),
                    .action = MenuAction::Connect,
                    .enabled = state.canConnect});
    entries.append({.id = QStringLiteral("session.disconnect"),
                    .parentId = QStringLiteral("session"),
                    .text = QCoreApplication::translate("GlobalMenu", "Disconnect"),
                    .iconName = QStringLiteral("network-disconnect"),
                    .action = MenuAction::Disconnect,
                    .enabled = state.sessionActive});
    entries.append({.id = QStringLiteral("session.separator"),
                    .parentId = QStringLiteral("session"),
                    .separator = true});
    // During active remote input Ctrl+Q must reach the Windows partner instead
    // of closing the controller. The toolbar/menu action remains available; its
    // accelerator returns as soon as input is disabled or the session ends.
    entries.append({.id = QStringLiteral("session.quit"),
                    .parentId = QStringLiteral("session"),
                    .text = QCoreApplication::translate("GlobalMenu", "Quit"),
                    .iconName = QStringLiteral("application-exit"),
                    .shortcut = (state.connected && state.inputEnabled)
                        ? QKeySequence{}
                        : QKeySequence(QKeySequence::Quit),
                    .action = MenuAction::Quit});

    // ----------------------------------------------------------------- View
    entries.append({.id = QStringLiteral("view"), .text = QCoreApplication::translate("GlobalMenu", "View"), .isMenu = true});
    entries.append({.id = QStringLiteral("view.input"),
                    .parentId = QStringLiteral("view"),
                    .text = QCoreApplication::translate("GlobalMenu", "Remote input"),
                    .iconName = QStringLiteral("input-keyboard"),
                    .action = MenuAction::ToggleInput,
                    .checkable = true,
                    .checked = state.inputEnabled,
                    .enabled = state.connected});
    entries.append({.id = QStringLiteral("view.statistics"),
                    .parentId = QStringLiteral("view"),
                    .text = QCoreApplication::translate("GlobalMenu", "Statistics"),
                    .iconName = QStringLiteral("view-statistics"),
                    .action = MenuAction::ToggleStatistics,
                    .checkable = true,
                    .checked = state.statisticsVisible,
                    .enabled = state.sessionActive});

    // ------------------------------------------------------------- Settings
    entries.append(
        {.id = QStringLiteral("settings"), .text = QCoreApplication::translate("GlobalMenu", "Settings"), .isMenu = true});
    entries.append({.id = QStringLiteral("settings.configure"),
                    .parentId = QStringLiteral("settings"),
                    .text = QCoreApplication::translate("GlobalMenu", "Configure Ultraview…"),
                    .iconName = QStringLiteral("configure"),
                    .action = MenuAction::Preferences});

    // ----------------------------------------------------------------- Help
    entries.append({.id = QStringLiteral("help"), .text = QCoreApplication::translate("GlobalMenu", "Help"), .isMenu = true});
    entries.append({.id = QStringLiteral("help.about"),
                    .parentId = QStringLiteral("help"),
                    .text = QCoreApplication::translate("GlobalMenu", "About Ultraview…"),
                    .iconName = QStringLiteral("help-about"),
                    .action = MenuAction::About});

    return entries;
}

GlobalMenu::GlobalMenu(models::ConnectionViewModel *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
    if (m_controller == nullptr)
        return;

    // Everything the menu says about the session comes from the controller.
    connect(m_controller, &models::ConnectionViewModel::stateChanged, this, &GlobalMenu::refreshMenu);
    connect(m_controller, &models::ConnectionViewModel::inputEnabledChanged, this,
            &GlobalMenu::refreshMenu);
    connect(m_controller, &models::ConnectionViewModel::partnerIdChanged, this,
            &GlobalMenu::refreshMenu);
    connect(m_controller, &models::ConnectionViewModel::settingsChanged, this,
            &GlobalMenu::refreshMenu);
}

GlobalMenu::~GlobalMenu()
{
#if defined(ULTRAVIEW_HAVE_GLOBAL_MENU)
    // The menu bar owns its root menu and the items representing our top-level
    // menus, so delete it first: that clears their pointers back into the menus
    // we own below.  The install-side D-Bus registration goes away with it.
    delete m_menuBar;
    m_menuBar = nullptr;

    for (const Binding &binding : std::as_const(m_bindings)) {
        if (binding.menu != nullptr)
            binding.menu->removeMenuItem(binding.item);
        delete binding.item;
    }
    m_bindings.clear();

    for (QPlatformMenu *menu : std::as_const(m_menus))
        delete menu;
    m_menus.clear();
    m_topLevelMenus.clear();
#endif
    // Without the private QPA menu API nothing was ever created.
}

void GlobalMenu::setStatisticsVisible(bool visible)
{
    if (m_statisticsVisible == visible)
        return;
    m_statisticsVisible = visible;
    emit statisticsVisibleChanged();
    refreshMenu();
}

void GlobalMenu::attach(QWindow *window)
{
    if (window == nullptr || m_window == window)
        return;

    m_window = window;
    createMenuBar();
    if (m_menuBar == nullptr)
        return;

    disconnect(m_visibilityConnection);
    m_visibilityConnection = connect(m_window, &QWindow::visibleChanged, this,
                                     &GlobalMenu::onWindowVisibilityChanged);
    onWindowVisibilityChanged();
}

void GlobalMenu::activate(MenuAction action)
{
    switch (action) {
    case MenuAction::None:
        break;
    case MenuAction::Connect:
        if (m_controller != nullptr)
            m_controller->connectNow();
        break;
    case MenuAction::Disconnect:
        if (m_controller != nullptr)
            m_controller->disconnectNow();
        break;
    case MenuAction::ToggleInput:
        if (m_controller != nullptr)
            m_controller->setInputEnabled(!m_controller->inputEnabled());
        break;
    case MenuAction::ToggleStatistics:
        setStatisticsVisible(!m_statisticsVisible);
        break;
    case MenuAction::Preferences:
        emit settingsRequested();
        break;
    case MenuAction::About:
        emit aboutRequested();
        break;
    case MenuAction::Quit:
        emit quitRequested();
        break;
    }
}

MenuState GlobalMenu::currentState() const
{
    MenuState state;
    state.statisticsVisible = m_statisticsVisible;
    if (m_controller == nullptr)
        return state;

    state.canConnect = m_controller->canConnect();
    state.connected = m_controller->isConnected();
    state.sessionActive = state.connected || m_controller->isBusy();
    state.inputEnabled = m_controller->inputEnabled();
    return state;
}

void GlobalMenu::createMenuBar()
{
    if (m_menuBar != nullptr)
        return;

#if defined(ULTRAVIEW_HAVE_GLOBAL_MENU)
    // Qt's Unix platform themes create a QDBusMenuBar when the session bus
    // provides the AppMenu registrar; on Plasma the KDE platform theme forwards
    // to the same implementation.  A null result simply means "no global menu
    // on this desktop" (or `KDE_NO_GLOBAL_MENU` was set to disable it).
    if (canAnnounceMenuBar()) {
        if (QPlatformTheme *theme = QGuiApplicationPrivate::platformTheme())
            m_menuBar = theme->createPlatformMenuBar();
    }
#endif

    if (m_menuBar == nullptr)
        return;

    buildMenu();
    refreshMenu();
    emit availabilityChanged();
}

void GlobalMenu::buildMenu()
{
#if defined(ULTRAVIEW_HAVE_GLOBAL_MENU)
    // Tags identify top-level menus to the menu bar (and items to their menu);
    // they have to be unique, the same way QMenuBar tags what it adds.
    quintptr nextTag = 1;

    const QVector<MenuItemDescription> entries = describeMenu(currentState());

    for (const MenuItemDescription &entry : entries) {
        if (!entry.isMenu)
            continue;
        QPlatformMenu *menu = m_menuBar->createMenu();
        menu->setTag(nextTag++);
        menu->setText(entry.text);
        if (!entry.iconName.isEmpty())
            menu->setIcon(QIcon::fromTheme(entry.iconName));
        menu->setEnabled(entry.enabled);
        menu->setVisible(true);
        m_menuBar->insertMenu(menu, nullptr);
        m_menus.append(menu);
        m_topLevelMenus.insert(entry.id, menu);
    }

    for (const MenuItemDescription &entry : entries) {
        if (entry.isMenu)
            continue;
        QPlatformMenu *parent = m_topLevelMenus.value(entry.parentId);
        if (parent == nullptr)
            continue;

        QPlatformMenuItem *item = parent->createMenuItem();
        item->setTag(nextTag++);
        if (entry.separator) {
            item->setIsSeparator(true);
        } else {
            item->setText(entry.text);
            if (!entry.iconName.isEmpty())
                item->setIcon(QIcon::fromTheme(entry.iconName));
            item->setCheckable(entry.checkable);
            item->setChecked(entry.checked);
            item->setEnabled(entry.enabled);
            item->setVisible(true);
            if (!entry.shortcut.isEmpty())
                item->setShortcut(entry.shortcut);
        }
        connect(item, &QPlatformMenuItem::activated, this,
                [this, action = entry.action] { activate(action); });
        parent->insertMenuItem(item, nullptr);
        m_bindings.insert(entry.id,
                          Binding{item, parent, entry.enabled, entry.checked, entry.shortcut});
    }
#endif
}

void GlobalMenu::refreshMenu()
{
#if defined(ULTRAVIEW_HAVE_GLOBAL_MENU)
    if (m_menuBar == nullptr || m_bindings.isEmpty())
        return;

    // The layout never changes; only the state of the items does.  Push the
    // changes to D-Bus (and only those) so an open menu follows the session.
    const QVector<MenuItemDescription> entries = describeMenu(currentState());
    for (const MenuItemDescription &entry : entries) {
        const auto found = m_bindings.find(entry.id);
        if (found == m_bindings.end())
            continue;
        Binding &binding = found.value();
        if (binding.enabled == entry.enabled && binding.checked == entry.checked
            && binding.shortcut == entry.shortcut) {
            continue;
        }
        binding.enabled = entry.enabled;
        binding.checked = entry.checked;
        binding.shortcut = entry.shortcut;
        binding.item->setEnabled(entry.enabled);
        if (entry.checkable)
            binding.item->setChecked(entry.checked);
        binding.item->setShortcut(entry.shortcut);
        binding.menu->syncMenuItem(binding.item);
    }
#endif
}

void GlobalMenu::registerWithWindow()
{
#if defined(ULTRAVIEW_HAVE_GLOBAL_MENU)
    if (m_menuBar == nullptr || m_registeredWithWindow || m_window.isNull())
        return;
    // Registration is bound to the platform window: an X11 window id, or the
    // wl_surface the `org_kde_kwin_appmenu` object is created for.
    if (m_window->handle() == nullptr)
        return;

    m_menuBar->handleReparent(m_window.data());
    m_registeredWithWindow = true;
    m_attached = true;
#endif
}

void GlobalMenu::onWindowVisibilityChanged()
{
    if (m_window.isNull())
        return;
    if (!m_window->isVisible()) {
        // Hiding the window destroys its platform window (and with it the
        // association on the window manager side), so the menu has to be handed
        // over again when the window comes back.
        m_registeredWithWindow = false;
        return;
    }
    registerWithWindow();
}

} // namespace ultraview::platform
