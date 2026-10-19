#pragma once
//
// Input translation on the controlling side.
//
// Turns QML/Qt input events into the recovered ASCII input commands (see
// protocol/InputCodec) and hands them to the session.  It is a QML type so the
// viewer page can drive it directly; the command bytes are built in C++ and the
// QML only forwards events.
//
// State that must not leak: a key or button that is pressed when the session ends
// (or when the window loses focus) has to be released, otherwise the partner keeps
// the button down.  `releaseAll()` exists for exactly that and the viewer calls it
// on disconnect and on focus loss; this mirrors the `finally` block of the
// reference viewer loop.
//
#include <QByteArray>
#include <QObject>
#include <QSet>
#include <QHash>
#include "input/KeyboardMapper.h"

#include <cstdint>

namespace ultraview::input {

class InputController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int remoteButtonState READ remoteButtonState NOTIFY stateChanged)
    Q_PROPERTY(bool anyInputHeld READ anyInputHeld NOTIFY stateChanged)

public:
    explicit InputController(QObject *parent = nullptr);
    ~InputController() override;

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    int remoteButtonState() const { return m_buttonMask; }
    bool anyInputHeld() const { return !m_pressedKeys.isEmpty() || !m_pressedButtons.isEmpty(); }

    /// Move the remote pointer.  Coordinates are remote desktop pixels; the codec
    /// validates the -32768..32767 range and refuses anything else.
    Q_INVOKABLE void moveTo(qreal x, qreal y);
    /// `button` is a Qt::MouseButton value (Qt::LeftButton == 1, ...).
    Q_INVOKABLE void press(int button);
    Q_INVOKABLE void release(int button);
    /// `delta` follows the browser/WM_MOUSEWHEEL convention: +120 per notch
    /// towards the user's "up" (the recovered viewer sends +/-120).
    Q_INVOKABLE void wheel(int delta);
    /// Returns false when the key has no Windows virtual-key equivalent.
    Q_INVOKABLE bool keyPress(int qtKey, int modifiers, const QString &text, bool autoRepeat);
    Q_INVOKABLE bool keyRelease(int qtKey, int modifiers);
    /// Release every key/button this controller pressed (focus loss, disconnect).
    Q_INVOKABLE void releaseAll();

signals:
    void commandReady(const QByteArray &payload);
    /// Non-fatal report for the status bar (unmapped key, refused coordinate).
    void statusMessage(const QString &message);
    void enabledChanged();
    void stateChanged();

private:
    void emitCommand(const QByteArray &payload);
    void fail(const QString &message);
    static int buttonFlag(int button);

    bool m_enabled = true;
    int m_buttonMask = 0;
    QSet<int> m_pressedButtons;
    QHash<int, KeyMapping> m_pressedKeys;
};

} // namespace ultraview::input
