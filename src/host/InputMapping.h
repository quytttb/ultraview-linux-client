#pragma once
//
// Windows input message to Linux key/pointer mapping.
//
// The viewer sends screen-canvas coordinates and Windows virtual-key/scan-code
// pairs.  The Wayland and X11 backends share this mapping; the numeric tables
// are the recovered US-layout mapping from the reference (X11 keysyms and evdev
// set-1 scan codes).
//
#include "host/HostProtocol.h"

#include <cstdint>
#include <optional>

namespace ultraview::host {

/// Windows mouse messages used by the viewer.
constexpr quint32 kMouseMoveMessage = 0x200;
constexpr quint32 kMouseWheelMessage = 0x20a;
constexpr quint8 kVirtualKeyPacket = 0xe7;

struct PortalKey {
    enum class Kind { Keysym, Keycode };
    Kind kind = Kind::Keysym;
    qint32 value = 0;
};

std::optional<PortalKey> mapKey(quint8 kind, quint8 virtualKey, quint16 scanCode,
                                quint16 character);

struct PointerAction {
    enum class Kind { None, Move, Button, Wheel };
    Kind kind = Kind::None;
    qint32 x = 0;
    qint32 y = 0;
    quint8 button = 0; ///< X button number: 1 left, 2 middle, 3 right
    bool pressed = false;
    qint32 steps = 0;  ///< positive = away from the user
};

/// Prefix rule: button message coordinates are viewer-client coordinates, so a
/// button acts at the last motion; the wheel accumulates 120-unit notches.
PointerAction pointerAction(const HostInputEvent &event, qint32 *wheelRemainder);

} // namespace ultraview::host
