#pragma once
//
// Qt key -> Windows virtual-key mapping.
//
// The recovered input command carries a Windows virtual-key code (`vk` in
// 0..255, pinned by `reference/test_input_control.py`), while Qt delivers its own
// key codes.  The translation table below is *client-side* glue recovered from the
// standard Windows keyboard layout; it is not part of the wire format and every
// entry is unit-tested locally (tests/tst_keyboard_mapper.cpp).
//
// Keys that cannot be mapped (media keys, dead keys without text, ...) are
// reported as unmapped instead of being silently dropped, so the UI can tell the
// user which key did not reach the partner.
//
#include <QString>

#include <cstdint>
#include <optional>

namespace ultraview::input {

struct KeyMapping {
    /// Windows virtual-key code, 0..255 (the wire range).
    quint8 virtualKey = 0;
    /// Windows delivers Alt combinations as WM_SYSKEYDOWN/WM_SYSKEYUP.
    bool system = false;
};

class KeyboardMapper {
public:
    /// Map a Qt key event.  `modifiers` decides the system-key flag, `text` (the
    /// character the platform produced, if any) resolves layout-dependent OEM keys.
    /// Returns nullopt when the key has no Windows virtual-key equivalent.
    static std::optional<KeyMapping> mapQtKey(int qtKey, Qt::KeyboardModifiers modifiers,
                                              const QString &text = QString());

    /// Human readable name of the virtual key for the diagnostics panel.
    static QString virtualKeyName(quint8 virtualKey);
};

} // namespace ultraview::input
