//
// Application entry point.
//
// The GUI is pure QML; everything it touches is a C++ type registered here (the
// QML module `Ultraview` carries the QML files, the types come from
// `ultraview_core` and the application target).
//
// `--self-test` loads the window offscreen, feeds deterministic screen patches
// into the render item and verifies that
//   * the QML module and every registered type loaded,
//   * the item mirrored the submitted patches byte for byte,
//   * the scene graph consumed the pending patches (and, when a working graphics
//     backend is available, that the grabbed window shows the patched colour).
// That is what CI can check without a real UltraViewer partner.
//
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QEventLoop>
#include <QFile>
#include <QTimer>

#include <cstdio>
#include <memory>

#include "core/SessionManager.h"
#include "core/SessionState.h"
#include "models/ConnectionViewModel.h"
#include "models/RecentConnectionsModel.h"
#include "models/SettingsModel.h"
#include "platform/GlobalMenu.h"
#include "platform/WaylandShortcutInhibitor.h"
#include "render/RemoteScreenItem.h"

namespace {

void registerQmlTypes()
{
    using namespace ultraview;
    qmlRegisterType<render::RemoteScreenItem>("Ultraview", 1, 0, "RemoteScreen");
    qmlRegisterType<input::InputController>("Ultraview", 1, 0, "InputController");
    qmlRegisterUncreatableType<models::ConnectionViewModel>(
        "Ultraview", 1, 0, "ConnectionViewModel", QStringLiteral("provided by the application"));
    qmlRegisterUncreatableType<models::SettingsModel>("Ultraview", 1, 0, "Settings",
                                                      QStringLiteral("provided by the application"));
    qmlRegisterUncreatableType<models::RecentConnectionsModel>(
        "Ultraview", 1, 0, "RecentConnections", QStringLiteral("provided by the application"));
    qmlRegisterUncreatableType<SessionManager>("Ultraview", 1, 0, "Session",
                                               QStringLiteral("provided by the application"));
    qmlRegisterUncreatableType<platform::GlobalMenu>(
        "Ultraview", 1, 0, "GlobalMenu", QStringLiteral("provided by the application"));
}

QByteArray rgb888Bytes(const QImage &image)
{
    const QImage converted = image.convertToFormat(QImage::Format_RGB888);
    QByteArray bytes;
    bytes.reserve(converted.sizeInBytes());
    for (int row = 0; row < converted.height(); ++row) {
        bytes.append(reinterpret_cast<const char *>(converted.constScanLine(row)), converted.bytesPerLine());
    }
    return bytes;
}

int runSelfTest(QQuickWindow *window, ultraview::platform::GlobalMenu *menu, int mockFrames)
{
    using ultraview::decoder::FrameUpdate;
    using ultraview::render::RemoteScreenItem;

    // The themed icons are module resources; a mistyped or unembedded file is
    // only a warning at runtime, so verify them explicitly here.
    const QStringList iconNames = {
        QStringLiteral("chart"),        QStringLiteral("check"),   QStringLiteral("chevron-down"),
        QStringLiteral("chevron-up"),   QStringLiteral("close"),   QStringLiteral("eye"),
        QStringLiteral("eye-off"),      QStringLiteral("folder"),  QStringLiteral("gear"),
        QStringLiteral("info"),         QStringLiteral("keyboard"), QStringLiteral("monitor"),
        QStringLiteral("power"),
    };
    QStringList missingIcons;
    for (const QString &name : iconNames) {
        const QString path = QStringLiteral(":/qt/qml/Ultraview/qml/assets/icons/%1.svg").arg(name);
        if (!QFile::exists(path))
            missingIcons.append(name);
    }
    if (!missingIcons.isEmpty()) {
        std::fprintf(stderr, "self-test: missing icon resources: %s\n",
                     qPrintable(missingIcons.join(QLatin1String(", "))));
        return 6;
    }

    // Exercise the actual viewer page rather than the hidden StackLayout child.
    window->setProperty("devViewer", true);
    QCoreApplication::processEvents();

    auto *screen = window->findChild<RemoteScreenItem *>();
    if (screen == nullptr) {
        std::fprintf(stderr, "self-test: the QML scene has no RemoteScreen item\n");
        return 2;
    }

    const QSize canvas(64, 48);
    const QRect patchRect(4, 4, 8, 8);
    QImage expected(canvas, QImage::Format_RGB32);
    expected.fill(Qt::black);
    {
        QPainter painting(&expected);
        painting.fillRect(patchRect, QColor(255, 64, 0));
    }

    {
        auto update = std::make_shared<FrameUpdate>();
        update->sequence = 1;
        update->canvasSize = canvas;
        update->rect = QRect(QPoint(0, 0), canvas);
        update->fullFrame = true;
        update->patch = QImage(canvas, QImage::Format_RGB32);
        update->patch.fill(Qt::black);
        screen->submitUpdate(update);
    }
    for (int index = 0; index < mockFrames; ++index) {
        auto update = std::make_shared<FrameUpdate>();
        update->sequence = static_cast<quint64>(index) + 2;
        update->canvasSize = canvas;
        update->rect = patchRect;
        update->patch = QImage(patchRect.size(), QImage::Format_RGB32);
        update->patch.fill(QColor(255, 64, 0));
        screen->submitUpdate(update);
    }

    const bool mirrored = screen->mirrorRgb888() == rgb888Bytes(expected);
    if (!mirrored)
        std::fprintf(stderr, "self-test: the render item mirror does not match the submitted patches\n");

    window->show();
    {
        // Let the scene graph render a few frames (the render loop runs on its
        // own thread; grabbing requires at least one completed frame).
        QEventLoop loop;
        QTimer::singleShot(300, &loop, &QEventLoop::quit);
        loop.exec();
    }
    const QImage grabbed = window->grabWindow();

    // Desktops with an application menu (KDE Plasma and friends) find the menu
    // over D-Bus; handing the window over is all the application does.  A menu
    // that is available but was not exported would leave the panel widget empty,
    // so treat that as a failure of this check.
    const bool menuAvailable = menu != nullptr && menu->isAvailable();
    const bool menuExported = menuAvailable && menu->isAttached();
    if (menuAvailable && !menuExported)
        std::fprintf(stderr, "self-test: the desktop offers a global menu but it was not exported\n");

    const bool consumed = screen->pendingTexturePatches() == 0;
    bool pixelOk = false;
    if (!grabbed.isNull()) {
        // The patch maps to a known spot on the item; find it in window pixels.
        const QPointF scene = screen->mapToScene(screen->mapFromSource(patchRect.center().x(),
                                                                      patchRect.center().y()));
        const qreal scale = window->width() > 0 ? qreal(grabbed.width()) / window->width() : 1.0;
        const QPoint probe(qRound(scene.x() * scale), qRound(scene.y() * scale));
        if (grabbed.rect().contains(probe)) {
            const QColor color = grabbed.pixelColor(probe);
            pixelOk = color.red() > 200 && color.green() < 120 && color.blue() < 60;
        }
    }

    std::printf("self-test: mirror=%s consumed=%s grab=%s pixel=%s menu=%s window=%dx%d\n",
                mirrored ? "ok" : "mismatch", consumed ? "ok" : "pending",
                grabbed.isNull() ? "unavailable" : "ok",
                grabbed.isNull() ? "unchecked" : (pixelOk ? "ok" : "mismatch"),
                menuAvailable ? (menuExported ? "exported" : "pending") : "unavailable",
                window->width(), window->height());

    if (!mirrored)
        return 3;
    if (!grabbed.isNull() && !consumed) {
        std::fprintf(stderr, "self-test: the scene graph did not consume the pending patches\n");
        return 4;
    }
    if (!grabbed.isNull() && !pixelOk) {
        std::fprintf(stderr, "self-test: the rendered window does not show the patched pixels\n");
        return 5;
    }
    if (menuAvailable && !menuExported)
        return 7;
    std::printf("self-test: ok\n");
    return 0;
}

// Development aid for UI review: render the real window (offscreen works) and
// save the frame as an image.  `view`, `dialog` and `tab` select one of the
// application surfaces so a documentation screenshot can be taken of it.
// `--screenshot-statistics` goes through the global menu (the viewer panel and
// the menu entry stay one state).  No network or profile access is involved.
int runScreenshot(QQuickWindow *window, const QString &path, const QString &view,
                  const QString &dialog, int dialogTab, bool fullScreen)
{
    window->show();
    // Show first so the QPA plugin creates the platform surface, then request
    // full screen; calling show() after showFullScreen() would restore Windowed.
    if (fullScreen) {
        window->setProperty("fullScreen", true);
        window->showFullScreen();
    }
    {
        // Give the scene graph a few frames to render before grabbing.
        QEventLoop loop;
        QTimer::singleShot(300, &loop, &QEventLoop::quit);
        loop.exec();
    }

    if (view == QLatin1String("viewer"))
        window->setProperty("devViewer", true);
    if (dialogTab > 0)
        window->setProperty("devDialogTab", dialogTab);
    if (!dialog.isEmpty())
        window->setProperty("devDialog", dialog);

    {
        // Let the pages lay out and the dialog/panel animations settle.
        QEventLoop loop;
        QTimer::singleShot(450, &loop, &QEventLoop::quit);
        loop.exec();
    }

    const QImage grabbed = window->grabWindow();
    if (grabbed.isNull()) {
        std::fprintf(stderr, "screenshot: the window could not be grabbed\n");
        return 2;
    }
    if (path.isEmpty() || !grabbed.save(path)) {
        std::fprintf(stderr, "screenshot: cannot write %s\n", qPrintable(path));
        return 3;
    }
    std::printf("screenshot: %s (%dx%d)\n", qPrintable(path), grabbed.width(), grabbed.height());
    return 0;
}

// Development aid for the first live check against the service:
//
//   QT_QPA_PLATFORM=offscreen ./build/ultraview --connect-attempt <partner-id>
//
// Exactly one attempt runs through the same `ConnectionViewModel` path the
// Connect button uses (settings validation, registration profile, TLS policy and
// timeouts included) and the outcome is printed, so a service answer - the
// status-11 refusal of an unknown partner, for instance - can be read without a
// screen.  Nothing is persisted and no credential is stored, which is why the
// documented invocation uses a throw-away configuration directory; a real
// partner session belongs in the interface, where the password is typed.
int runConnectAttempt(ultraview::models::ConnectionViewModel *controller, const QString &partnerId)
{
    using SessionState = ultraview::SessionState;

    // The attempt writes settings (a recent-connection entry, the credential
    // preference) and would otherwise reach the normal profile and its keyring,
    // so it only runs with a redirected configuration directory.
    if (qEnvironmentVariableIsEmpty("XDG_CONFIG_HOME")) {
        std::fprintf(stderr,
                     "connect-attempt: set XDG_CONFIG_HOME to a throw-away directory; the probe "
                     "must not write the normal profile or its credential store\n");
        return 1;
    }

    controller->setPartnerId(partnerId);
    if (!controller->canConnect()) {
        const QString reason = controller->configurationHint().isEmpty()
            ? controller->statusText()
            : controller->configurationHint();
        std::fprintf(stderr, "connect-attempt: cannot start: %s\n", qPrintable(reason));
        return 1;
    }
    // A probe must not leave a credential entry behind for a partner nobody is
    // signing in to, and it takes no credential from the caller.  The client
    // builds its request with a password, but that password only takes part in
    // the relay login: a probe that is refused before the relay (the usual case)
    // never sends it, and a partner that does answer refuses the placeholder.
    controller->settingsObject()->setRememberPassword(false);
    controller->setPassword(QStringLiteral("ultraview-probe-placeholder"));

    std::printf("connect-attempt: partner=%s password=placeholder (no credential)\n",
                qPrintable(partnerId));
    std::fflush(stdout);

    controller->connectNow();

    // The worker reports from its own thread through queued signals, so the state
    // is polled instead of assuming which signal arrives last.
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(100);
    bool settled = false;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        const int code = controller->stateCode();
        const bool active = code == int(SessionState::Connecting)
            || code == int(SessionState::Registering)
            || code == int(SessionState::RequestingPartner)
            || code == int(SessionState::RelayConnecting)
            || code == int(SessionState::Authenticating);
        if (code == int(SessionState::Connected) || code == int(SessionState::Failed)
            || (!active && !controller->errorText().isEmpty())) {
            settled = true; // connected, refused by the server, or rejected locally
            loop.quit();
        }
    });
    poll.start();
    constexpr int attemptTimeoutMs = 120000;
    QTimer::singleShot(attemptTimeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    poll.stop();

    // Let the queued error and statistics signals arrive before reading them.
    {
        QEventLoop drain;
        QTimer::singleShot(400, &drain, &QEventLoop::quit);
        drain.exec();
    }

    std::printf("connect-attempt: state=%s(%d) connected=%s desktop=%s error=\"%s\"\n",
                qPrintable(controller->stateName()), controller->stateCode(),
                controller->isConnected() ? "yes" : "no", qPrintable(controller->desktopSizeText()),
                qPrintable(controller->errorText()));
    std::fflush(stdout);

    if (!settled) {
        std::fprintf(stderr, "connect-attempt: no outcome within %d ms\n", attemptTimeoutMs);
        return 2;
    }
    // A refusal counts as an outcome: the exit code only says whether the probe
    // produced a verdict, the printed state carries the verdict itself.
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ultraview"));
    QCoreApplication::setApplicationName(QStringLiteral("ultraview"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ULTRAVIEW_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("UltraViewer (native Linux client)"));
    // The UI is fully themed in QML; the Basic style keeps Qt from injecting its
    // own look on top of the theme.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    ultraview::SessionManager::registerMetaTypes();
    registerQmlTypes();

    ultraview::models::ConnectionViewModel controller;
    // The desktop's application menu (KDE global menu) mirrors the session
    // actions; on desktops without one it stays inert.
    ultraview::platform::GlobalMenu globalMenu(&controller);

    const QStringList arguments = QCoreApplication::arguments();
    // Development aid: one live connect attempt without any window (docs/testing.md);
    // it needs no QML and runs before the interface is loaded.
    if (const int index = arguments.indexOf(QStringLiteral("--connect-attempt"));
        index >= 0 && index + 1 < arguments.size())
        return runConnectAttempt(&controller, arguments.at(index + 1));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("globalMenu"), &globalMenu);
    engine.loadFromModule("Ultraview", "Main");
    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "the QML module 'Ultraview' could not be loaded\n");
        return 1;
    }

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (window == nullptr) {
        std::fprintf(stderr, "the QML root object is not a window\n");
        return 1;
    }
    // Registration waits for the platform window, so this is safe before the
    // window is on screen; the menu is announced again whenever it is shown.
    globalMenu.attach(window);

#if defined(ULTRAVIEW_HAVE_WAYLAND_SHORTCUT_INHIBIT)
    // On Wayland/KWin, an active remote-control session asks the compositor to
    // deliver Super/Meta and other desktop shortcut keys to this window. The QML
    // viewer then forwards them to the partner instead of opening local menus.
    ultraview::platform::WaylandShortcutInhibitor shortcutInhibitor(&controller);
    shortcutInhibitor.attach(window);
#endif

    if (arguments.contains(QStringLiteral("--self-test"))) {
        int mockFrames = 4;
        if (const int index = arguments.indexOf(QStringLiteral("--mock-frames"));
            index >= 0 && index + 1 < arguments.size()) {
            mockFrames = arguments.at(index + 1).toInt();
        }
        return runSelfTest(window, &globalMenu, mockFrames);
    }

    // Development aid for UI review: render the real window (offscreen works) and
    // save the frame as an image.  No network or profile access is involved.
    if (const int index = arguments.indexOf(QStringLiteral("--screenshot"));
        index >= 0 && index + 1 < arguments.size()) {
        QString view;
        if (const int viewIndex = arguments.indexOf(QStringLiteral("--screenshot-view"));
            viewIndex >= 0 && viewIndex + 1 < arguments.size()) {
            view = arguments.at(viewIndex + 1);
        }
        QString dialog;
        if (const int dialogIndex = arguments.indexOf(QStringLiteral("--screenshot-dialog"));
            dialogIndex >= 0 && dialogIndex + 1 < arguments.size()) {
            dialog = arguments.at(dialogIndex + 1);
        }
        int dialogTab = 0;
        if (const int tabIndex = arguments.indexOf(QStringLiteral("--screenshot-tab"));
            tabIndex >= 0 && tabIndex + 1 < arguments.size()) {
            dialogTab = arguments.at(tabIndex + 1).toInt();
        }
        // The statistics panel is a menu action; drive it through the same path
        // so the exported menu and the viewer stay in sync.
        if (arguments.contains(QStringLiteral("--screenshot-statistics")))
            globalMenu.setStatisticsVisible(true);
        return runScreenshot(window, arguments.at(index + 1), view, dialog, dialogTab,
                             arguments.contains(QStringLiteral("--screenshot-fullscreen")));
    }

    return application.exec();
}
