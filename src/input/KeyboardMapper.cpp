#include "input/KeyboardMapper.h"

#include <QHash>
#include <Qt>

namespace ultraview::input {
namespace {

/// Named keys: Qt::Key -> Windows virtual-key code.
const QHash<int, quint8> &namedKeys()
{
    static const QHash<int, quint8> table = {
        {Qt::Key_Escape, 0x1B},      {Qt::Key_Tab, 0x09},        {Qt::Key_Backtab, 0x09},
        {Qt::Key_Backspace, 0x08},   {Qt::Key_Return, 0x0D},     {Qt::Key_Enter, 0x0D},
        {Qt::Key_Insert, 0x2D},      {Qt::Key_Delete, 0x2E},     {Qt::Key_Home, 0x24},
        {Qt::Key_End, 0x23},         {Qt::Key_PageUp, 0x21},     {Qt::Key_PageDown, 0x22},
        {Qt::Key_Left, 0x25},        {Qt::Key_Up, 0x26},         {Qt::Key_Right, 0x27},
        {Qt::Key_Down, 0x28},        {Qt::Key_Print, 0x2C},      {Qt::Key_SysReq, 0x2C},
        {Qt::Key_Pause, 0x13},       {Qt::Key_NumLock, 0x90},    {Qt::Key_CapsLock, 0x14},
        {Qt::Key_ScrollLock, 0x91},  {Qt::Key_Menu, 0x5D},       {Qt::Key_Help, 0x2F},
        {Qt::Key_Clear, 0x0C},       {Qt::Key_Select, 0x29},     {Qt::Key_Space, 0x20},
        {Qt::Key_Shift, 0x10},       {Qt::Key_Control, 0x11},    {Qt::Key_Alt, 0x12},
        {Qt::Key_AltGr, 0xA5},       {Qt::Key_Meta, 0x5B},       {Qt::Key_Super_L, 0x5B},
        {Qt::Key_Super_R, 0x5C},     {Qt::Key_Hyper_L, 0x5B},    {Qt::Key_Hyper_R, 0x5C},
        {Qt::Key_Multi_key, 0x5D},   {Qt::Key_VolumeDown, 0xAE}, {Qt::Key_VolumeUp, 0xAF},
        {Qt::Key_VolumeMute, 0xAD},  {Qt::Key_MediaStop, 0xB2},  {Qt::Key_MediaPlay, 0xB3},
        {Qt::Key_MediaNext, 0xB0},   {Qt::Key_MediaPrevious, 0xB1},
    };
    return table;
}

/// Printable characters that Windows maps to the OEM keys.
const QHash<QChar, quint8> &oemKeys()
{
    static const QHash<QChar, quint8> table = {
        {QLatin1Char(';'), 0xBA}, {QLatin1Char(':'), 0xBA}, {QLatin1Char('='), 0xBB},
        {QLatin1Char('+'), 0xBB}, {QLatin1Char(','), 0xBC}, {QLatin1Char('<'), 0xBC},
        {QLatin1Char('-'), 0xBD}, {QLatin1Char('_'), 0xBD}, {QLatin1Char('.'), 0xBE},
        {QLatin1Char('>'), 0xBE}, {QLatin1Char('/'), 0xBF}, {QLatin1Char('?'), 0xBF},
        {QLatin1Char('`'), 0xC0}, {QLatin1Char('~'), 0xC0}, {QLatin1Char('['), 0xDB},
        {QLatin1Char('{'), 0xDB}, {QLatin1Char('\\'), 0xDC}, {QLatin1Char('|'), 0xDC},
        {QLatin1Char(']'), 0xDD}, {QLatin1Char('}'), 0xDD}, {QLatin1Char('\''), 0xDE},
        {QLatin1Char('"'), 0xDE},
    };
    return table;
}

std::optional<quint8> keypadKey(int qtKey, Qt::KeyboardModifiers modifiers)
{
    if (!modifiers.testFlag(Qt::KeypadModifier))
        return std::nullopt;
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
        return static_cast<quint8>(0x60 + (qtKey - Qt::Key_0)); // VK_NUMPAD0..9
    switch (qtKey) {
    case Qt::Key_Asterisk:
        return 0x6A; // VK_MULTIPLY
    case Qt::Key_Plus:
        return 0x6B; // VK_ADD
    case Qt::Key_Minus:
        return 0x6D; // VK_SUBTRACT
    case Qt::Key_Period:
    case Qt::Key_Comma:
        return 0x6E; // VK_DECIMAL (the separator key of the numeric keypad)
    case Qt::Key_Slash:
        return 0x6F; // VK_DIVIDE
    case Qt::Key_Enter:
    case Qt::Key_Return:
        return 0x0D;
    default:
        return std::nullopt;
    }
}

} // namespace

std::optional<KeyMapping> KeyboardMapper::mapQtKey(int qtKey, Qt::KeyboardModifiers modifiers,
                                                   const QString &text)
{
    KeyMapping mapping;
    // Windows reports Alt combinations (including the Alt key itself) as system
    // key events; the recovered codec exposes that through its `system` flag.
    mapping.system = modifiers.testFlag(Qt::AltModifier) || qtKey == Qt::Key_Alt;

    if (const auto keypad = keypadKey(qtKey, modifiers)) {
        mapping.virtualKey = *keypad;
        return mapping;
    }

    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        mapping.virtualKey = static_cast<quint8>(0x41 + (qtKey - Qt::Key_A));
        return mapping;
    }
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
        mapping.virtualKey = static_cast<quint8>(0x30 + (qtKey - Qt::Key_0));
        return mapping;
    }
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24) {
        mapping.virtualKey = static_cast<quint8>(0x70 + (qtKey - Qt::Key_F1));
        return mapping;
    }
    if (const auto named = namedKeys().constFind(qtKey); named != namedKeys().constEnd()) {
        mapping.virtualKey = *named;
        return mapping;
    }
    // Layout dependent printable keys: prefer the character the platform produced,
    // because Qt's key code for them follows the local layout.
    if (!text.isEmpty()) {
        if (const auto oem = oemKeys().constFind(text.at(0)); oem != oemKeys().constEnd()) {
            mapping.virtualKey = *oem;
            return mapping;
        }
    }
    if (qtKey >= 0x20 && qtKey <= 0x7E) {
        const auto oem = oemKeys().constFind(QChar(static_cast<ushort>(qtKey)));
        if (oem != oemKeys().constEnd()) {
            mapping.virtualKey = *oem;
            return mapping;
        }
    }
    return std::nullopt;
}

QString KeyboardMapper::virtualKeyName(quint8 virtualKey)
{
    if (virtualKey >= 0x30 && virtualKey <= 0x39)
        return QStringLiteral("Digit %1").arg(
            QChar(QLatin1Char(static_cast<char>('0' + (virtualKey - 0x30)))));
    if (virtualKey >= 0x41 && virtualKey <= 0x5A)
        return QStringLiteral("Key %1").arg(
            QChar(QLatin1Char(static_cast<char>('A' + (virtualKey - 0x41)))));
    if (virtualKey >= 0x70 && virtualKey <= 0x87)
        return QStringLiteral("F%1").arg(virtualKey - 0x70 + 1);
    if (virtualKey >= 0x60 && virtualKey <= 0x69)
        return QStringLiteral("Numpad %1").arg(virtualKey - 0x60);
    switch (virtualKey) {
    case 0x08:
        return QStringLiteral("Backspace");
    case 0x09:
        return QStringLiteral("Tab");
    case 0x0D:
        return QStringLiteral("Enter");
    case 0x10:
        return QStringLiteral("Shift");
    case 0x11:
        return QStringLiteral("Control");
    case 0x12:
        return QStringLiteral("Alt");
    case 0x13:
        return QStringLiteral("Pause");
    case 0x14:
        return QStringLiteral("Caps Lock");
    case 0x1B:
        return QStringLiteral("Escape");
    case 0x20:
        return QStringLiteral("Space");
    case 0x21:
        return QStringLiteral("Page Up");
    case 0x22:
        return QStringLiteral("Page Down");
    case 0x23:
        return QStringLiteral("End");
    case 0x24:
        return QStringLiteral("Home");
    case 0x25:
        return QStringLiteral("Left");
    case 0x26:
        return QStringLiteral("Up");
    case 0x27:
        return QStringLiteral("Right");
    case 0x28:
        return QStringLiteral("Down");
    case 0x2C:
        return QStringLiteral("Print Screen");
    case 0x2D:
        return QStringLiteral("Insert");
    case 0x2E:
        return QStringLiteral("Delete");
    case 0x5B:
        return QStringLiteral("Left Windows");
    case 0x5C:
        return QStringLiteral("Right Windows");
    case 0x5D:
        return QStringLiteral("Menu");
    case 0x90:
        return QStringLiteral("Num Lock");
    case 0x91:
        return QStringLiteral("Scroll Lock");
    case 0xBA:
        return QStringLiteral(";");
    case 0xBB:
        return QStringLiteral("=");
    case 0xBC:
        return QStringLiteral(",");
    case 0xBD:
        return QStringLiteral("-");
    case 0xBE:
        return QStringLiteral(".");
    case 0xBF:
        return QStringLiteral("/");
    case 0xC0:
        return QStringLiteral("`");
    case 0xDB:
        return QStringLiteral("[");
    case 0xDC:
        return QStringLiteral("\\");
    case 0xDD:
        return QStringLiteral("]");
    case 0xDE:
        return QStringLiteral("'");
    default:
        break;
    }
    // Hex digits are upper case, the prefix stays lower case.
    return QStringLiteral("VK 0x%1").arg(
        QString::number(virtualKey, 16).rightJustified(2, QLatin1Char('0')).toUpper());
}

} // namespace ultraview::input
