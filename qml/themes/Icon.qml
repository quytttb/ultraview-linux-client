pragma Singleton
import QtQuick

// Icon URLs for the bundled monochrome SVG assets.  The paths are resolved
// relative to this file so they stay valid wherever the QML module is deployed
// from (build tree, installed resources or a package).
//
// The assets are drawn inside this project (see docs/ui.md); they carry the
// theme's muted stroke colour and are used at their natural colour, so no
// colourising effect module is required.
QtObject {
    readonly property url gear: Qt.resolvedUrl("../assets/icons/gear.svg")
    readonly property url info: Qt.resolvedUrl("../assets/icons/info.svg")
    readonly property url eye: Qt.resolvedUrl("../assets/icons/eye.svg")
    readonly property url eyeOff: Qt.resolvedUrl("../assets/icons/eye-off.svg")
    readonly property url folder: Qt.resolvedUrl("../assets/icons/folder.svg")
    readonly property url chevronDown: Qt.resolvedUrl("../assets/icons/chevron-down.svg")
    readonly property url chevronUp: Qt.resolvedUrl("../assets/icons/chevron-up.svg")
    readonly property url check: Qt.resolvedUrl("../assets/icons/check.svg")
    readonly property url close: Qt.resolvedUrl("../assets/icons/close.svg")
    readonly property url windowMinimize: Qt.resolvedUrl("../assets/icons/window-minimize.svg")
    readonly property url windowMaximize: Qt.resolvedUrl("../assets/icons/window-maximize.svg")
    readonly property url windowRestore: Qt.resolvedUrl("../assets/icons/window-restore.svg")
    readonly property url monitor: Qt.resolvedUrl("../assets/icons/monitor.svg")
    readonly property url keyboard: Qt.resolvedUrl("../assets/icons/keyboard.svg")
    readonly property url chart: Qt.resolvedUrl("../assets/icons/chart.svg")
    readonly property url power: Qt.resolvedUrl("../assets/icons/power.svg")
}
