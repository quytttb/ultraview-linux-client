pragma Singleton
import QtQuick

// Central theme: every colour and metric the UI uses lives here so the pages stay
// declarative and consistent.  The palette is deliberately low-chroma: the remote
// desktop is the content, the local chrome must stay out of the way.
QtObject {
    id: theme

    readonly property bool dark: true

    // surfaces
    readonly property color windowBackground: dark ? "#0e1116" : "#f4f6f9"
    readonly property color surface: dark ? "#141922" : "#ffffff"
    readonly property color surfaceAlt: dark ? "#1b212c" : "#eef1f5"
    readonly property color surfaceHover: dark ? "#232c39" : "#e3e8ef"
    readonly property color surfaceSunken: dark ? "#10141b" : "#f7f9fc"
    readonly property color viewerBackground: "#000000"

    // borders
    readonly property color border: dark ? "#232b37" : "#d3d9e2"
    readonly property color borderStrong: dark ? "#334050" : "#bcc5d2"
    readonly property color focusRing: Qt.rgba(0.30, 0.56, 1.0, 0.30)

    // text
    readonly property color text: dark ? "#e7ecf3" : "#1a1f26"
    readonly property color textMuted: dark ? "#9aa6b6" : "#5b6673"
    readonly property color textFaint: dark ? "#6c7789" : "#7a8494"
    readonly property color textInverted: "#ffffff"

    // accents
    readonly property color accent: "#4c8dff"
    readonly property color accentHover: "#6ea4ff"
    readonly property color accentPressed: "#3b78e6"
    readonly property color accentText: "#9dc0ff"
    readonly property color accentSoft: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.14)
    readonly property color success: "#35c377"
    readonly property color warning: "#f0a63a"
    readonly property color danger: "#f0614f"
    readonly property color dangerText: "#ff9285"

    // metrics
    readonly property int radiusSmall: 8
    readonly property int radius: 12
    readonly property int radiusLarge: 16
    readonly property int radiusPill: 999
    readonly property int spacingXSmall: 4
    readonly property int spacingSmall: 8
    readonly property int spacing: 14
    readonly property int spacingLarge: 24
    readonly property int controlHeight: 38
    readonly property int toolbarHeight: 56
    readonly property int fontTiny: 11
    readonly property int fontSmall: 12
    readonly property int fontNormal: 14
    readonly property int fontHeading: 17
    readonly property int fontTitle: 24
    readonly property int durationFast: 120
    readonly property int duration: 180

    function tinted(base, alpha) {
        return Qt.rgba(base.r, base.g, base.b, alpha)
    }

    // status colours
    function statusColor(stateName) {
        switch (stateName) {
        case "Connected":
            return theme.success
        case "Failed":
            return theme.danger
        case "Disconnected":
            return theme.textMuted
        default:
            return theme.warning
        }
    }

    function statusSoft(stateName) {
        return theme.tinted(theme.statusColor(stateName), 0.15)
    }
}
