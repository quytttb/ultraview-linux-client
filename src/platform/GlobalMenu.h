#pragma once
//
// KDE global menu support (the `com.canonical.dbusmenu` application menu).
//
// The user interface of this client is a QML toolbar, so the application never
// exports an application menu on its own and the KDE "Global Menu" panel widget
// (or a global menu button in a window decoration) has nothing to show.  Plasma
// learns about application menus over D-Bus: an application publishes a
// `com.canonical.dbusmenu` object and points the window manager at it (an X11
// window property, or the `org_kde_kwin_appmenu` Wayland protocol).  Qt 6.9
// implements that in QtGui's Unix platform theme, so this class only has to
// describe *which* menu the application offers and hand the menu bar to the main
// window - the D-Bus object, the window-manager registration and the item
// updates are Qt's job.
//
// The QPA menu API is private Qt API (`Qt6::GuiPrivate`): CMake defines
// `ULTRAVIEW_HAVE_GLOBAL_MENU` only when Qt >= 6.9 and the private headers are
// installed.  Without them this class still exists (QML binds to
// `statisticsVisible`), it simply never exports a menu.
//
// The menu mirrors the actions the pages offer, and nothing else: the viewer
// toolbar keeps its buttons.
//
#include <QHash>
#include <QKeySequence>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
class QPlatformMenu;
class QPlatformMenuBar;
class QPlatformMenuItem;
class QWindow;
QT_END_NAMESPACE

namespace ultraview::models {
class ConnectionViewModel;
}

namespace ultraview::platform {

/// Actions a global menu item can trigger.
enum class MenuAction {
    None,
    Connect,
    Disconnect,
    Quit,
    ToggleInput,
    ToggleStatistics,
    Preferences,
    About,
};

/// Everything the menu needs to know about the application.  A plain snapshot
/// so the layout can be checked without a session.
struct MenuState {
    bool canConnect = false;        ///< partner data complete, no session yet
    bool sessionActive = false;     ///< connecting or connected
    bool connected = false;         ///< authenticated, screen updates flowing
    bool inputEnabled = true;       ///< the user's input preference
    bool statisticsVisible = false; ///< the viewer's statistics panel
};

/// One line of the menu, in the order the platform layer has to render it: a
/// top-level menu (`isMenu`) is always listed before its children.
struct MenuItemDescription {
    QString id{};       ///< stable identifier, used to match items across updates
    QString parentId{}; ///< empty for a top-level menu
    QString text{};
    QString iconName{}; ///< freedesktop icon name, empty when the item has no icon
    QKeySequence shortcut{};
    MenuAction action = MenuAction::None;
    bool isMenu = false;
    bool separator = false;
    bool checkable = false;
    bool checked = false;
    bool enabled = true;
};

/// The menu layout for a session state.  Pure function: this is what the unit
/// tests pin down; `GlobalMenu` renders and updates it.
QVector<MenuItemDescription> describeMenu(const MenuState &state);

/// Exports the application menu of the QML window and keeps it in sync with the
/// session state.  KDE's global menu shows the result; nothing is displayed
/// in-window, so the QML toolbar stays as it is.
class GlobalMenu : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ isAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(bool statisticsVisible READ statisticsVisible WRITE setStatisticsVisible NOTIFY
                   statisticsVisibleChanged)

public:
    explicit GlobalMenu(models::ConnectionViewModel *controller, QObject *parent = nullptr);
    ~GlobalMenu() override;

    /// True when the desktop offers a global menu, i.e. when the platform theme
    /// created a menu bar (the session bus provides the AppMenu registrar and Qt
    /// is new enough) and the platform can announce it for a window.  False
    /// means "nothing to do", not an error.
    bool isAvailable() const { return m_menuBar != nullptr; }
    /// True once the menu bar was announced for the window.
    bool isAttached() const { return m_attached; }

    bool statisticsVisible() const { return m_statisticsVisible; }
    void setStatisticsVisible(bool visible);

    /// Export the menu for @p window.  Safe to call before the window is on
    /// screen: registration waits for the platform window (and is repeated when
    /// the window is shown again, which recreates it).
    void attach(QWindow *window);

    /// Run the action behind a menu item.  The menu items and the tests both go
    /// through here, so the behaviour is observable without a global menu.
    void activate(MenuAction action);

signals:
    void availabilityChanged();
    void statisticsVisibleChanged();
    /// The dialogs and the window belong to QML; these signals ask for them.
    void settingsRequested();
    void aboutRequested();
    void quitRequested();

private:
    struct Binding {
        QPlatformMenuItem *item = nullptr;
        QPlatformMenu *menu = nullptr; ///< the menu the item lives in
        bool enabled = true;
        bool checked = false;
        QKeySequence shortcut;
    };

    void createMenuBar();
    void buildMenu();
    void refreshMenu();
    void registerWithWindow();
    void onWindowVisibilityChanged();
    MenuState currentState() const;

    models::ConnectionViewModel *m_controller = nullptr;
    QPointer<QWindow> m_window;
    QMetaObject::Connection m_visibilityConnection;
    QPlatformMenuBar *m_menuBar = nullptr;
    QHash<QString, QPlatformMenu *> m_topLevelMenus;
    QHash<QString, Binding> m_bindings;
    QVector<QPlatformMenu *> m_menus;
    bool m_registeredWithWindow = false;
    bool m_attached = false;
    bool m_statisticsVisible = false;
};

} // namespace ultraview::platform
