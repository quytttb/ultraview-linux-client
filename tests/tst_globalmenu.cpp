//
// Global menu tests: the menu layout the KDE application menu shows, the actions
// behind its items, and the `com.canonical.dbusmenu` object Qt exports for the
// window.
//
// The D-Bus part needs a session bus and a desktop that offers an application
// menu (the `com.canonical.AppMenu.Registrar` service); without either, the test
// checks that nothing is exported and that nothing crashes.  Nothing here talks
// to a network: the session actions are only exercised while no session can
// start.
//
#include "models/ConnectionViewModel.h"
#include "platform/GlobalMenu.h"

#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStringList>
#include <QWindow>
#include <QtTest>

#if defined(ULTRAVIEW_TEST_DBUS)
#  include <QDBusArgument>
#  include <QDBusConnection>
#  include <QDBusMessage>
#  include <QDBusVariant>
#  include <QXmlStreamReader>
#  include <optional>
#endif

using namespace ultraview;
using namespace ultraview::platform;

namespace {

const MenuItemDescription *findEntry(const QVector<MenuItemDescription> &entries, const QString &id)
{
    for (const MenuItemDescription &entry : entries) {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

void checkEnabled(const QVector<MenuItemDescription> &entries, const QString &id, bool expected)
{
    const MenuItemDescription *entry = findEntry(entries, id);
    QVERIFY2(entry != nullptr, qPrintable(QStringLiteral("no menu entry %1").arg(id)));
    QCOMPARE(entry->enabled, expected);
}

void checkChecked(const QVector<MenuItemDescription> &entries, const QString &id, bool expected)
{
    const MenuItemDescription *entry = findEntry(entries, id);
    QVERIFY2(entry != nullptr, qPrintable(QStringLiteral("no menu entry %1").arg(id)));
    QCOMPARE(entry->checkable, true);
    QCOMPARE(entry->checked, expected);
}

/// Titles of the top-level menus, in order.
QStringList menuTitles(const QVector<MenuItemDescription> &entries)
{
    QStringList titles;
    for (const MenuItemDescription &entry : entries) {
        if (entry.isMenu)
            titles.append(entry.text);
    }
    return titles;
}

/// Titles of the entries of one top-level menu, separators left out.
QStringList itemTitles(const QVector<MenuItemDescription> &entries, const QString &menuId)
{
    QStringList titles;
    for (const MenuItemDescription &entry : entries) {
        if (!entry.isMenu && !entry.separator && entry.parentId == menuId)
            titles.append(entry.text);
    }
    return titles;
}

#if defined(ULTRAVIEW_TEST_DBUS)
/// One `(ia{sv}av)` item of a dbusmenu layout, as the panel widget reads it.
struct LayoutItem {
    int id = 0;
    QVariantMap properties;
    QList<LayoutItem> children;
};

LayoutItem readLayoutItem(const QDBusArgument &argument)
{
    // The read API of QDBusArgument is the const overload set: the non-const
    // begin/end pair is for marshalling and refuses read-only arguments.
    LayoutItem item;
    argument.beginStructure();
    argument >> item.id >> item.properties;
    argument.beginArray();
    while (!argument.atEnd()) {
        QDBusVariant child;
        argument >> child;
        const QDBusArgument childArgument = child.variant().value<QDBusArgument>();
        item.children.append(readLayoutItem(childArgument));
    }
    argument.endArray();
    argument.endStructure();
    return item;
}

/// Values of `a{sv}` properties arrive wrapped in a variant.
QVariant unwrap(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        return value.value<QDBusVariant>().variant();
    return value;
}

QString labelOf(const LayoutItem &item)
{
    return unwrap(item.properties.value(QStringLiteral("label"))).toString();
}

bool isSeparator(const LayoutItem &item)
{
    return unwrap(item.properties.value(QStringLiteral("type"))).toString()
        == QLatin1String("separator");
}

QStringList labelsOf(const QList<LayoutItem> &items)
{
    QStringList labels;
    for (const LayoutItem &item : items) {
        if (!isSeparator(item))
            labels.append(labelOf(item));
    }
    return labels;
}

const LayoutItem *findLayoutItem(const LayoutItem &root, const QString &label)
{
    for (const LayoutItem &child : root.children) {
        if (labelOf(child) == label)
            return &child;
        if (const LayoutItem *nested = findLayoutItem(child, label); nested != nullptr)
            return nested;
    }
    return nullptr;
}

/// Child object names of one object path on our own D-Bus connection.
QStringList introspectionChildren(const QString &path)
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage message = QDBusMessage::createMethodCall(
        bus.baseService(), path, QStringLiteral("org.freedesktop.DBus.Introspectable"),
        QStringLiteral("Introspect"));
    const QDBusMessage reply = bus.call(message);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};

    QXmlStreamReader reader(reply.arguments().constFirst().toString());
    QStringList children;
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement() || reader.name() != QLatin1String("node"))
            continue;
        const QString name = reader.attributes().value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            children.append(name);
    }
    return children;
}

/// The exported menu bar: the object path Qt registered and the layout a global
/// menu host asks for.  Qt registers each menu bar as `/MenuBar/<n>` on the
/// session bus, so the object is looked up instead of guessing the number.
struct ExportedMenu {
    QString path;
    LayoutItem layout;
};

std::optional<ExportedMenu> readExportedMenu()
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return std::nullopt;

    if (!introspectionChildren(QStringLiteral("/")).contains(QStringLiteral("MenuBar")))
        return std::nullopt;
    const QStringList bars = introspectionChildren(QStringLiteral("/MenuBar"));
    if (bars.isEmpty())
        return std::nullopt;

    ExportedMenu exported;
    exported.path = QStringLiteral("/MenuBar/") + bars.constFirst();

    QDBusMessage call = QDBusMessage::createMethodCall(
        bus.baseService(), exported.path, QStringLiteral("com.canonical.dbusmenu"),
        QStringLiteral("GetLayout"));
    call.setArguments({0, -1, QStringList()});
    const QDBusMessage reply = bus.call(call);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2)
        return std::nullopt;

    const QDBusArgument layout = reply.arguments().at(1).value<QDBusArgument>();
    exported.layout = readLayoutItem(layout);
    return exported;
}
#endif // ULTRAVIEW_TEST_DBUS

} // namespace

class TestGlobalMenu : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void menuLayout();
    void menuLayoutStructure();
    void activation();
    void windowLifecycle();
    void exportedMenu();
};

void TestGlobalMenu::initTestCase()
{
    // The point of this test is the menu, not the user's configuration: keep
    // everything the client persists inside the Qt test directories.
    QStandardPaths::setTestModeEnabled(true);
}

void TestGlobalMenu::menuLayout()
{
    // Nothing configured and nothing running: this is what the panel shows.
    const QVector<MenuItemDescription> idle = describeMenu(MenuState());
    QCOMPARE(menuTitles(idle), QStringList({QStringLiteral("Session"), QStringLiteral("View"),
                                            QStringLiteral("Settings"), QStringLiteral("Help")}));
    QCOMPARE(itemTitles(idle, QStringLiteral("session")),
             QStringList({QStringLiteral("Connect"), QStringLiteral("Disconnect"),
                          QStringLiteral("Quit")}));
    QCOMPARE(itemTitles(idle, QStringLiteral("view")),
             QStringList({QStringLiteral("Remote input"), QStringLiteral("Statistics")}));
    QCOMPARE(itemTitles(idle, QStringLiteral("settings")),
             QStringList({QStringLiteral("Configure Ultraview…")}));
    QCOMPARE(itemTitles(idle, QStringLiteral("help")),
             QStringList({QStringLiteral("About Ultraview…")}));

    // Nothing to connect to and no session: only the always-useful entries are
    // enabled.
    checkEnabled(idle, QStringLiteral("session.connect"), false);
    checkEnabled(idle, QStringLiteral("session.disconnect"), false);
    checkEnabled(idle, QStringLiteral("session.quit"), true);
    checkEnabled(idle, QStringLiteral("view.input"), false);
    checkEnabled(idle, QStringLiteral("view.statistics"), false);
    checkEnabled(idle, QStringLiteral("settings.configure"), true);
    checkEnabled(idle, QStringLiteral("help.about"), true);

    // The two toggles follow the session: input needs a connected session,
    // statistics only needs the viewer to be on screen.
    MenuState connecting;
    connecting.sessionActive = true;
    const QVector<MenuItemDescription> busy = describeMenu(connecting);
    checkEnabled(busy, QStringLiteral("view.input"), false);
    checkChecked(busy, QStringLiteral("view.input"), true);
    checkEnabled(busy, QStringLiteral("view.statistics"), true);
    checkEnabled(busy, QStringLiteral("session.disconnect"), true);
    checkEnabled(busy, QStringLiteral("session.connect"), false);

    MenuState connected;
    connected.sessionActive = true;
    connected.connected = true;
    connected.inputEnabled = false;
    connected.statisticsVisible = true;
    const QVector<MenuItemDescription> live = describeMenu(connected);
    checkEnabled(live, QStringLiteral("view.input"), true);
    checkChecked(live, QStringLiteral("view.input"), false);
    checkEnabled(live, QStringLiteral("view.statistics"), true);
    checkChecked(live, QStringLiteral("view.statistics"), true);

    // While remote input is enabled, Ctrl+Q belongs to the Windows partner;
    // the menu still offers Quit but must not reserve the accelerator locally.
    connected.inputEnabled = true;
    const QVector<MenuItemDescription> inputLive = describeMenu(connected);
    const MenuItemDescription *inputLiveQuit =
        findEntry(inputLive, QStringLiteral("session.quit"));
    QVERIFY(inputLiveQuit != nullptr);
    QVERIFY(inputLiveQuit->shortcut.isEmpty());
    const MenuItemDescription *inputDisabledQuit =
        findEntry(live, QStringLiteral("session.quit"));
    QVERIFY(inputDisabledQuit != nullptr);
    QCOMPARE(inputDisabledQuit->shortcut, QKeySequence(QKeySequence::Quit));

    // With a partner id and a profile, Connect becomes the only way in.
    MenuState ready;
    ready.canConnect = true;
    const QVector<MenuItemDescription> configured = describeMenu(ready);
    checkEnabled(configured, QStringLiteral("session.connect"), true);
    checkEnabled(configured, QStringLiteral("session.disconnect"), false);
}

void TestGlobalMenu::menuLayoutStructure()
{
    const QVector<MenuItemDescription> entries = describeMenu(MenuState());

    // Every entry is either a top-level menu or belongs to one that was listed
    // before it; the platform layer renders the list in order.
    QStringList menus;
    for (const MenuItemDescription &entry : entries) {
        if (entry.isMenu) {
            QVERIFY(!menus.contains(entry.id));
            QVERIFY(entry.parentId.isEmpty());
            QVERIFY(!entry.text.isEmpty());
            menus.append(entry.id);
            continue;
        }
        QVERIFY2(menus.contains(entry.parentId), qPrintable(entry.id + QStringLiteral(": parent")));
    }
    QCOMPARE(menus.size(), 4);

    // The session menu separates the session actions from Quit.
    int separators = 0;
    for (const MenuItemDescription &entry : entries) {
        if (!entry.separator) {
            QVERIFY2(!entry.text.isEmpty(), qPrintable(entry.id));
            if (!entry.isMenu) {
                QVERIFY2(!entry.iconName.isEmpty(), qPrintable(entry.id));
                QVERIFY(entry.action != MenuAction::None);
            }
            continue;
        }
        ++separators;
        QVERIFY(entry.text.isEmpty());
        QCOMPARE(entry.action, MenuAction::None);
    }
    QCOMPARE(separators, 1);

    // Quit advertises the shortcut the window implements (Ctrl+Q).
    const MenuItemDescription *quit = findEntry(entries, QStringLiteral("session.quit"));
    QVERIFY(quit != nullptr);
    QCOMPARE(quit->shortcut, QKeySequence(QKeySequence::Quit));

    // Only the two toggles are checkable.
    for (const MenuItemDescription &entry : entries) {
        if (entry.checkable)
            continue;
        QCOMPARE(entry.checked, false);
    }
}

void TestGlobalMenu::activation()
{
    models::ConnectionViewModel controller;
    GlobalMenu menu(&controller);

    QSignalSpy settingsSpy(&menu, &GlobalMenu::settingsRequested);
    QSignalSpy aboutSpy(&menu, &GlobalMenu::aboutRequested);
    QSignalSpy quitSpy(&menu, &GlobalMenu::quitRequested);

    // The dialogs and the window belong to QML; the menu only asks for them.
    menu.activate(MenuAction::Preferences);
    menu.activate(MenuAction::About);
    menu.activate(MenuAction::Quit);
    QCOMPARE(settingsSpy.count(), 1);
    QCOMPARE(aboutSpy.count(), 1);
    QCOMPARE(quitSpy.count(), 1);

    // The statistics toggle is the menu's own state: the viewer page binds to it.
    QCOMPARE(menu.statisticsVisible(), false);
    menu.activate(MenuAction::ToggleStatistics);
    QCOMPARE(menu.statisticsVisible(), true);
    QSignalSpy statisticsSpy(&menu, &GlobalMenu::statisticsVisibleChanged);
    menu.activate(MenuAction::ToggleStatistics);
    QCOMPARE(menu.statisticsVisible(), false);
    QCOMPARE(statisticsSpy.count(), 1);

    // Input goes through the controller, exactly like the toolbar button.
    const bool inputEnabled = controller.inputEnabled();
    menu.activate(MenuAction::ToggleInput);
    QCOMPARE(controller.inputEnabled(), !inputEnabled);
    menu.activate(MenuAction::ToggleInput);
    QCOMPARE(controller.inputEnabled(), inputEnabled);

    // No session is started here: without a partner id the controller refuses
    // before it opens a socket, and it says why.
    menu.activate(MenuAction::Disconnect);
    QCOMPARE(controller.isBusy(), false);
    QCOMPARE(controller.isConnected(), false);
    if (controller.partnerId().isEmpty()) {
        QSignalSpy rejected(&controller, &models::ConnectionViewModel::errorRaised);
        menu.activate(MenuAction::Connect);
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(controller.isBusy(), false);
        QVERIFY(!controller.errorText().isEmpty());
    }
}

void TestGlobalMenu::windowLifecycle()
{
    models::ConnectionViewModel controller;
    GlobalMenu menu(&controller);

    // A window that is not on screen yet has no platform window to register
    // with; the menu has to wait for it instead of failing.
    QWindow late;
    late.resize(200, 150);
    menu.attach(&late);
    QCOMPARE(menu.isAttached(), false);

    {
        QWindow window;
        window.resize(320, 200);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        menu.attach(&window);
        QCOMPARE(menu.isAttached(), menu.isAvailable());

        // Attaching twice, and hiding/showing the window (which recreates the
        // platform window), must be harmless.
        menu.attach(&window);
        window.hide();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(menu.isAttached(), menu.isAvailable());
    }

    // The window is gone: the menu must survive without touching it.
    QCoreApplication::processEvents();
    QCOMPARE(menu.isAttached(), menu.isAvailable());

    late.show();
    QVERIFY(QTest::qWaitForWindowExposed(&late));
    QCOMPARE(menu.isAttached(), menu.isAvailable());
}

void TestGlobalMenu::exportedMenu()
{
    models::ConnectionViewModel controller;
    GlobalMenu menu(&controller);

    QWindow window;
    window.resize(320, 200);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    menu.attach(&window);

    if (!menu.isAvailable())
        QSKIP("this session does not offer a global menu (com.canonical.AppMenu.Registrar)");
    QVERIFY(menu.isAttached());

#if defined(ULTRAVIEW_TEST_DBUS)
    std::optional<ExportedMenu> exported = readExportedMenu();
    QVERIFY2(exported.has_value(),
             "no com.canonical.dbusmenu object is registered on the session bus");

    // This is the menu the panel widget will show.
    QCOMPARE(labelsOf(exported->layout.children),
             QStringList({QStringLiteral("Session"), QStringLiteral("View"),
                          QStringLiteral("Settings"), QStringLiteral("Help")}));
    const LayoutItem *session = findLayoutItem(exported->layout, QStringLiteral("Session"));
    QVERIFY(session != nullptr);
    QCOMPARE(labelsOf(session->children),
             QStringList({QStringLiteral("Connect"), QStringLiteral("Disconnect"),
                          QStringLiteral("Quit")}));
    const LayoutItem *view = findLayoutItem(exported->layout, QStringLiteral("View"));
    QVERIFY(view != nullptr);
    QCOMPARE(labelsOf(view->children),
             QStringList({QStringLiteral("Remote input"), QStringLiteral("Statistics")}));

    // Separators travel as a type, not as a label.
    int separators = 0;
    for (const LayoutItem &item : session->children) {
        if (isSeparator(item))
            ++separators;
    }
    QCOMPARE(separators, 1);

    // Item state has to follow the application while the menu is exported.
    const LayoutItem *statistics = findLayoutItem(exported->layout, QStringLiteral("Statistics"));
    QVERIFY(statistics != nullptr);
    QCOMPARE(unwrap(statistics->properties.value(QStringLiteral("toggle-type"))).toString(),
             QStringLiteral("checkmark"));
    QCOMPARE(unwrap(statistics->properties.value(QStringLiteral("toggle-state"))).toInt(), 0);
    QCOMPARE(unwrap(statistics->properties.value(QStringLiteral("enabled"))).toBool(), false);

    menu.setStatisticsVisible(true);
    exported = readExportedMenu();
    QVERIFY(exported.has_value());
    statistics = findLayoutItem(exported->layout, QStringLiteral("Statistics"));
    QVERIFY(statistics != nullptr);
    QCOMPARE(unwrap(statistics->properties.value(QStringLiteral("toggle-state"))).toInt(), 1);

    // The shortcut of Quit is advertised to the panel.
    const LayoutItem *quit = findLayoutItem(exported->layout, QStringLiteral("Quit"));
    QVERIFY(quit != nullptr);
    QVERIFY(quit->properties.contains(QStringLiteral("shortcut")));

    // On X11 the window is announced through the session's AppMenu registrar
    // instead of the Wayland compositor protocol: ask it which menu belongs to
    // our window.  (Wayland has no equivalent bus API; there the compositor
    // holds the address, which the live check in verification.md inspects with
    // WAYLAND_DEBUG.)
    if (QGuiApplication::platformName() == QLatin1String("xcb")) {
        QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("com.canonical.AppMenu.Registrar"),
            QStringLiteral("/com/canonical/AppMenu/Registrar"),
            QStringLiteral("com.canonical.AppMenu.Registrar"),
            QStringLiteral("GetMenuForWindow"));
        call.setArguments({uint(window.winId())});
        const QDBusMessage reply = QDBusConnection::sessionBus().call(call);
        QVERIFY2(reply.type() == QDBusMessage::ReplyMessage, qPrintable(reply.errorName()));
        QCOMPARE(reply.arguments().size(), 2);
        QCOMPARE(reply.arguments().at(1).value<QDBusObjectPath>().path(), exported->path);
    }
#else
    QSKIP("built without QtDBus");
#endif
}

int main(int argc, char *argv[])
{
    // QGuiApplication is enough: this test never creates widgets.
    QGuiApplication application(argc, argv);
    TestGlobalMenu test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_globalmenu.moc"
