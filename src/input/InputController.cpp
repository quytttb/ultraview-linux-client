#include "input/InputController.h"

#include "core/ErrorTypes.h"
#include "input/KeyboardMapper.h"
#include "protocol/InputCodec.h"
#include "protocol/RegistrationCodec.h"

#include <QKeySequence>
#include <Qt>

#include <algorithm>

namespace ultraview::input {
namespace {

/// Maps a Qt mouse button to the recovered button names.
protocol::MouseButton wireButton(int button, bool *ok)
{
    switch (button) {
    case Qt::LeftButton:
        *ok = true;
        return protocol::MouseButton::Left;
    case Qt::RightButton:
        *ok = true;
        return protocol::MouseButton::Right;
    case Qt::MiddleButton:
        *ok = true;
        return protocol::MouseButton::Middle;
    default:
        *ok = false;
        return protocol::MouseButton::Left;
    }
}

/// The reference keeps the coordinate inside the Int16 wire range; a mouse that
/// leaves the viewport must not send an out-of-range value.
bool isValidCoordinate(qreal value)
{
    return value >= -32768.0 && value <= 32767.0;
}

int roundedCoordinate(qreal value)
{
    return static_cast<int>(value < 0 ? value - 0.5 : value + 0.5);
}

} // namespace

InputController::InputController(QObject *parent)
    : QObject(parent)
{
}

InputController::~InputController() = default;

void InputController::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    if (!enabled)
        releaseAll();
    m_enabled = enabled;
    emit enabledChanged();
}

int InputController::buttonFlag(int button)
{
    switch (button) {
    case Qt::LeftButton:
        return 0x1;
    case Qt::RightButton:
        return 0x2;
    case Qt::MiddleButton:
        return 0x4;
    default:
        return 0;
    }
}

void InputController::fail(const QString &message)
{
    emit statusMessage(message);
}

void InputController::emitCommand(const QByteArray &payload)
{
    if (!payload.isEmpty())
        emit commandReady(payload);
}

void InputController::moveTo(qreal x, qreal y)
{
    if (!m_enabled)
        return;
    if (!isValidCoordinate(x) || !isValidCoordinate(y)) {
        fail(tr("pointer position outside the supported range was not sent"));
        return;
    }
    try {
        emitCommand(protocol::InputCodec::mouseMove(roundedCoordinate(x), roundedCoordinate(y)));
    } catch (const UvException &exception) {
        fail(exception.message());
    }
}

void InputController::press(int button)
{
    if (!m_enabled)
        return;
    bool ok = false;
    const protocol::MouseButton wire = wireButton(button, &ok);
    if (!ok) {
        fail(tr("unsupported mouse button"));
        return;
    }
    try {
        emitCommand(protocol::InputCodec::mouseButton(wire, true));
    } catch (const UvException &exception) {
        fail(exception.message());
        return;
    }
    m_pressedButtons.insert(button);
    m_buttonMask |= buttonFlag(button);
    emit stateChanged();
}

void InputController::release(int button)
{
    if (!m_enabled)
        return;
    bool ok = false;
    const protocol::MouseButton wire = wireButton(button, &ok);
    if (!ok) {
        fail(tr("unsupported mouse button"));
        return;
    }
    try {
        emitCommand(protocol::InputCodec::mouseButton(wire, false));
    } catch (const UvException &exception) {
        fail(exception.message());
        return;
    }
    if (m_pressedButtons.remove(button) || m_buttonMask != 0) {
        m_buttonMask &= ~buttonFlag(button);
        emit stateChanged();
    }
}

void InputController::wheel(int delta)
{
    if (!m_enabled || delta == 0)
        return;
    try {
        emitCommand(protocol::InputCodec::mouseWheel(delta));
    } catch (const UvException &exception) {
        fail(exception.message());
    }
}

bool InputController::keyPress(int qtKey, int modifiers, const QString &text, bool autoRepeat)
{
    if (!m_enabled)
        return false;
    if (autoRepeat) {
        // Qt repeats the key while it is held; Windows does the same on its side,
        // so the repeat must not be forwarded twice.
        return true;
    }
    const auto mapping = KeyboardMapper::mapQtKey(qtKey, Qt::KeyboardModifiers(modifiers), text);
    if (!mapping.has_value()) {
        fail(tr("the key %1 has no Windows equivalent and was not sent")
                 .arg(QKeySequence(qtKey).toString(QKeySequence::NativeText)));
        return false;
    }
    try {
        emitCommand(protocol::InputCodec::keyboard(mapping->virtualKey, true, mapping->system));
    } catch (const UvException &exception) {
        fail(exception.message());
        return false;
    }
    m_pressedKeys.insert(qtKey, *mapping);
    emit stateChanged();
    return true;
}

bool InputController::keyRelease(int qtKey, int modifiers)
{
    if (!m_enabled)
        return false;
    Q_UNUSED(modifiers);
    const auto held = m_pressedKeys.constFind(qtKey);
    if (held == m_pressedKeys.constEnd())
        return false;
    // Preserve the actual down mapping: modifiers or layout text may have
    // changed before release (e.g. Alt released before F4, or an OEM key).
    const KeyMapping mapping = *held;
    try {
        emitCommand(protocol::InputCodec::keyboard(mapping.virtualKey, false, mapping.system));
    } catch (const UvException &exception) {
        fail(exception.message());
        return false;
    }
    m_pressedKeys.remove(qtKey);
    emit stateChanged();
    return true;
}

void InputController::releaseAll()
{
    const QHash<int, KeyMapping> keys = m_pressedKeys;
    const QSet<int> buttons = m_pressedButtons;
    m_pressedKeys.clear();
    m_pressedButtons.clear();
    m_buttonMask = 0;

    for (const KeyMapping &mapping : keys) {
        try {
            emitCommand(protocol::InputCodec::keyboard(mapping.virtualKey, false, mapping.system));
        } catch (const UvException &) {
            // Nothing left to do: the key could not be released on the wire.
        }
    }
    for (int button : buttons) {
        bool ok = false;
        const protocol::MouseButton wire = wireButton(button, &ok);
        if (!ok)
            continue;
        try {
            emitCommand(protocol::InputCodec::mouseButton(wire, false));
        } catch (const UvException &) {
        }
    }
    if (!keys.isEmpty() || !buttons.isEmpty())
        emit stateChanged();
}

} // namespace ultraview::input
