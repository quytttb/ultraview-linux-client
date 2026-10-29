# Interface (QML) — design system and development aids

The UI is QML only (`qml/`); protocol, decoding, rendering and input logic stay in
C++. This file documents the theme, the component inventory and the
development-only review tools. It describes this project's own interface; it
makes no claim about the vendor's Windows program.

## Style base

`QQuickStyle::setStyle("Basic")` is selected in `src/app/main.cpp`. The style is a
*base* only (control behaviour, keyboard handling, accessibility plumbing); its
visuals are replaced by the themed components below so the application does not
inherit whatever the distribution's default style happens to be. No extra QML
module is required at runtime — `QtQuick.Effects`, `Qt5Compat.GraphicalEffects`
and the Material/Universal styles are deliberately **not** used, which keeps the
package dependency list in `packaging/CPackConfig.cmake` unchanged. Elevation is
expressed with borders and surface colours instead of shader effects, so the
interface also works on older system Qt versions.

## Theme tokens — `qml/themes/Theme.qml`

Singleton; every colour and metric the interface uses lives there.

| Group | Tokens |
|---|---|
| Surfaces | `windowBackground`, `surface`, `surfaceAlt`, `surfaceHover`, `surfaceSunken`, `viewerBackground` |
| Borders | `border`, `borderStrong`, `focusRing` |
| Text | `text`, `textMuted`, `textFaint`, `textInverted` |
| Accent / state | `accent`, `accentHover`, `accentPressed`, `accentText`, `accentSoft`, `success`, `warning`, `danger`, `dangerText` |
| Metrics | `radiusSmall`/`radius`/`radiusLarge`/`radiusPill`, `spacingXSmall`…`spacingLarge`, `controlHeight`, `toolbarHeight`, `fontTiny`…`fontTitle`, `durationFast`/`duration` |
| Helpers | `tinted(colour, alpha)`, `statusColor(state)`, `statusSoft(state)` |

## Components — `qml/components/`

| Component | Role |
|---|---|
| `AppButton` | primary / secondary / ghost / danger variants plus the toggled-on (`active`) look; optional icon, animated hover/pressed |
| `IconButton` | square icon-only button with a themed tooltip (window chrome, dialog close) |
| `AppTextField`, `PasswordField`, `AppInlineField` | labelled input, password input with reveal toggle, dense grid input |
| `AppFieldBackground` | shared rounded input surface with hover state and focus halo |
| `AppCheckBox`, `AppComboBox`, `AppSpinBox`, `AppTabButton` | themed replacements for the Basic-style controls, whose indicator/chrome cannot be recoloured through the palette |
| `AppSpinner` | accent ring spinner shown while the first screen update is pending |
| `AppToolbar`, `ViewerToolbar` | application header and viewer session actions |
| `StatusBadge`, `StatRow`, `EmptyState`, `FilePicker` | status pill, statistics line, introductory block, path chooser |

`qml/pages/` (connect page, host page, viewer page) and `qml/dialogs/`
(settings, about, session log, host approval) only compose these components.

The status bar (window footer) shows the session state and the latest message.
That message is clickable: the complete session log opens in a dialog with the
build identity, the state timeline, every notice and the full error detail (TLS
certificate diagnostics included), plus a **Copy log** button. The log is kept in
memory only (most recent 200 entries) and never contains the partner password.
The footer follows the selected mode: controller states and the copyable session
log on **Control**, host state and status on **Host** (host status is not written
into the controller's log).

The header carries a **Control / Host** switch. It is disabled while a controller
session is live and while hosting, so a running role cannot be left running
invisibly. **Host** shows the local identity page; opening it loads (or creates)
the private host identity but never starts sharing. Only **Start hosting** starts
the registration, relay and screen-capture backend. The page shows the assigned
ID, the 8-digit password, local address, backend/layout diagnostics, the current
status, and a **Allow keyboard and mouse** switch once a viewer is connected.
Chat, clipboard, file transfer and audio are explicitly described as unsupported
on the host side. The approval dialog is modal, cannot be dismissed by clicking
outside, offers **Allow control**, **View only** and **Refuse**, and counts down
the 45-second request deadline; Escape refuses rather than approves.

The viewer toolbar carries the session state, **Disable input**, **Statistics**,
**Full screen** and **Disconnect**. Opening a session maximizes the window. Full
screen sends a real `QWindow::showFullScreen()` request to the compositor, hides
the application chrome, and leaves the remote screen unobstructed. The viewer
toolbar is then an auto-hiding floating overlay: move the pointer to the top edge
to reveal it, then choose **Exit full screen**; the previous maximized/windowed
state is restored rather than shrinking the window.

While remote input is on, application shortcuts such as Ctrl+Q, F11 and Esc are
not reserved locally; they go to the partner. On KDE Wayland, the standard
`keyboard-shortcuts-inhibit` protocol also prevents the compositor from consuming
Super/Meta and desktop shortcuts while this app has focus, so those keys can be
forwarded too. The toolbar is the deliberate local escape route. Other desktops
that do not offer this protocol cannot guarantee suppression of compositor-level
shortcuts. A drained registration channel closing after the route is logged as an
INFO diagnostic instead of replacing the *Connected* status with a warning.

## Icons — `qml/assets/icons/*.svg`

Thirteen monochrome SVGs drawn inside this project: no third-party icon set and no
additional licence obligation. `qml/themes/Icon.qml` resolves the file URLs
relative to itself, so they load from the build tree, from installed resources or
from a package unchanged. The files are embedded by
`qt_add_qml_module(... RESOURCES ...)`; `--self-test` asserts that all of them are
present, because a missing icon is otherwise only a runtime warning.

## Desktop application menu (KDE global menu)

The actions the pages offer are also exported as a genuine application menu, so
KDE's **Global Menu** panel widget (or a global-menu button in the window
decoration) shows `Session`, `View`, `Settings` and `Help` for this window. Qt
6.9 turned this into a QtGui feature: the Unix platform theme creates the
`com.canonical.dbusmenu` menu bar and announces it to the window manager (an X11
window property, or the `org_kde_kwin_appmenu` Wayland protocol). This project
therefore only describes *which* menu to offer — `src/platform/GlobalMenu.cpp`
owns the layout and the actions — and hands it to the main window; the D-Bus
object, the registration and the incremental item updates are Qt's.

| Menu | Items |
|---|---|
| Session | Connect, Disconnect, ─, Quit (Ctrl+Q) |
| View | Remote input (checkable), Statistics (checkable) |
| Settings | Configure Ultraview… |
| Help | About Ultraview… |

Connect/Disconnect and the two toggles are enabled according to the session
state, exactly like the toolbar buttons; Remote input mirrors
`controller.inputEnabled`, and Statistics is the same state the viewer toolbar's
button writes (`GlobalMenu::statisticsVisible` is the single source of truth for
the panel). The dialogs and the window belong to QML, so the menu asks for them
with `settingsRequested`/`aboutRequested`/`quitRequested` signals; `Main.qml`
also implements the advertised Ctrl+Q shortcut. While this window is focused the
window shortcut wins, so Ctrl+Q quits the client instead of being forwarded to the
partner — the same trade-off a menu accelerator has in any desktop application.

Using the QPA menu bar needs QtGui's private QPA headers, so the feature is
compiled in only when the distribution ships them (`qt6-base-private-dev` on
Debian/Ubuntu) and Qt is ≥6.9: `CMakeLists.txt` then defines
`ULTRAVIEW_HAVE_GLOBAL_MENU`. Everything degrades without it — no menu, no build
error — and the class exists either way, because QML binds to it. A menu bar is
also refused when the platform cannot announce it to a window manager (Qt's
offscreen plugin used by the tests, for instance): such a menu would be
invisible *and* Qt would crash while registering it. `KDE_NO_GLOBAL_MENU=1`
disables the export on the Plasma side without recompiling.

Nothing is drawn inside the window for this feature: the QML toolbar stays as it
is, and the menu exists only where the desktop asks for one.

## Development aids (not product features)

```sh
# render the real window offscreen and save the frame
QT_QPA_PLATFORM=offscreen ./build/ultraview --screenshot /tmp/connect.png

# other surfaces: viewer page, statistics panel, settings tab, about dialog
QT_QPA_PLATFORM=offscreen ./build/ultraview \
    --screenshot /tmp/viewer.png --screenshot-view viewer --screenshot-statistics
QT_QPA_PLATFORM=offscreen ./build/ultraview \
    --screenshot /tmp/host.png --screenshot-view host
QT_QPA_PLATFORM=offscreen ./build/ultraview \
    --screenshot /tmp/settings.png --screenshot-dialog settings --screenshot-tab 1
```

The flags only set the `devViewer`, `devDialog` and `devDialogTab` properties of
`Main.qml` before the frame is grabbed: no socket is opened and no profile is
read. `--screenshot-statistics` goes through `GlobalMenu::statisticsVisible`, the
same state the panel and the desktop menu share. Screenshots taken this way are
how the interface was reviewed on this machine (see `verification.md`).

Limits: offscreen rendering reproduces layout, typography and colours, but not
hover/pressed/focus transitions or tooltips, which need an interactive display
session.
