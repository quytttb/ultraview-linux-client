#include "host/InputMapping.h"

namespace ultraview::host {
namespace {

std::optional<qint32> vkKeysym(quint8 vk, quint16 scan)
{
    const bool extended = ((scan & 0xff00) == 0xe000) || ((scan & 0x0100) != 0);
    if (vk >= 0x30 && vk <= 0x39)
        return vk;
    if (vk >= 0x41 && vk <= 0x5a)
        return static_cast<qint32>(vk) + 0x20;
    if (vk >= 0x60 && vk <= 0x69)
        return 0xffb0 + (static_cast<qint32>(vk) - 0x60);
    if (vk >= 0x70 && vk <= 0x87)
        return 0xffbe + (static_cast<qint32>(vk) - 0x70);
    switch (vk) {
    case 0x08: return 0xff08;
    case 0x09: return 0xff09;
    case 0x0c: return 0xff0b;
    case 0x0d: return extended ? 0xff8d : 0xff0d;
    case 0x10: return (scan & 0xff) == 0x36 ? 0xffe2 : 0xffe1;
    case 0x11: return extended ? 0xffe4 : 0xffe3;
    case 0x12: return extended ? 0xffea : 0xffe9;
    case 0x13: return 0xff13;
    case 0x14: return 0xffe5;
    case 0x1b: return 0xff1b;
    case 0x20: return 0x20;
    case 0x21: return 0xff55;
    case 0x22: return 0xff56;
    case 0x23: return 0xff57;
    case 0x24: return 0xff50;
    case 0x25: return 0xff51;
    case 0x26: return 0xff52;
    case 0x27: return 0xff53;
    case 0x28: return 0xff54;
    case 0x2c: return 0xff61;
    case 0x2d: return 0xff63;
    case 0x2e: return 0xffff;
    case 0x5b: return 0xffeb;
    case 0x5c: return 0xffec;
    case 0x5d: return 0xff67;
    case 0x6a: return 0xffaa;
    case 0x6b: return 0xffab;
    case 0x6c: return 0xffac;
    case 0x6d: return 0xffad;
    case 0x6e: return 0xffae;
    case 0x6f: return 0xffaf;
    case 0x90: return 0xff7f;
    case 0x91: return 0xff14;
    case 0xa0: return 0xffe1;
    case 0xa1: return 0xffe2;
    case 0xa2: return 0xffe3;
    case 0xa3: return 0xffe4;
    case 0xa4: return 0xffe9;
    case 0xa5: return 0xffea;
    case 0xba: return 0x3b;
    case 0xbb: return 0x3d;
    case 0xbc: return 0x2c;
    case 0xbd: return 0x2d;
    case 0xbe: return 0x2e;
    case 0xbf: return 0x2f;
    case 0xc0: return 0x60;
    case 0xdb: return 0x5b;
    case 0xdc: return 0x5c;
    case 0xdd: return 0x5d;
    case 0xde: return 0x27;
    case 0xe2: return 0x3c;
    default: return std::nullopt;
    }
}

std::optional<qint32> charKeysym(quint16 character)
{
    if ((character >= 0x20 && character <= 0x7e)
        || (character >= 0xa0 && character <= 0xff)) {
        return character;
    }
    if (character <= 0x1f || (character >= 0x7f && character <= 0x9f)
        || (character >= 0xd800 && character <= 0xdfff)) {
        return std::nullopt;
    }
    return 0x01000000 | character;
}

std::optional<qint32> evdevKeycode(quint16 scan)
{
    const quint32 make = scan & 0xff;
    if ((scan & 0xff00) == 0)
        return (make >= 1 && make <= 0x58) ? std::optional<qint32>(make) : std::nullopt;
    if ((scan & 0xff00) != 0xe000 && (scan & 0x0100) == 0)
        return std::nullopt;
    switch (make) {
    case 0x1c: return 96;
    case 0x1d: return 97;
    case 0x35: return 98;
    case 0x37: return 99;
    case 0x38: return 100;
    case 0x47: return 102;
    case 0x48: return 103;
    case 0x49: return 104;
    case 0x4b: return 105;
    case 0x4d: return 106;
    case 0x4f: return 107;
    case 0x50: return 108;
    case 0x51: return 109;
    case 0x52: return 110;
    case 0x53: return 111;
    case 0x5b: return 125;
    case 0x5c: return 126;
    case 0x5d: return 127;
    default: return std::nullopt;
    }
}

} // namespace

std::optional<PortalKey> mapKey(quint8 kind, quint8 virtualKey, quint16 scanCode,
                                quint16 character)
{
    if (virtualKey == kVirtualKeyPacket) {
        if (const auto sym = charKeysym(character); sym.has_value())
            return PortalKey{PortalKey::Kind::Keysym, *sym};
        return std::nullopt;
    }
    if (const auto sym = vkKeysym(virtualKey, scanCode); sym.has_value())
        return PortalKey{PortalKey::Kind::Keysym, *sym};
    if (const auto code = evdevKeycode(scanCode); code.has_value())
        return PortalKey{PortalKey::Kind::Keycode, *code};
    (void)kind;
    return std::nullopt;
}

PointerAction pointerAction(const HostInputEvent &event, qint32 *wheelRemainder)
{
    if (event.type != HostInputType::Mouse)
        return {};
    switch (event.message) {
    case kMouseMoveMessage:
        return PointerAction{PointerAction::Kind::Move, event.x, event.y, 0, false, 0};
    case kMouseWheelMessage: {
        if (wheelRemainder == nullptr)
            return {};
        *wheelRemainder += event.wheel;
        const qint32 steps = *wheelRemainder / 120;
        *wheelRemainder %= 120;
        if (steps == 0)
            return {};
        return PointerAction{PointerAction::Kind::Wheel, 0, 0, 0, false, steps};
    }
    case 0x201:
    case 0x203:
        return PointerAction{PointerAction::Kind::Button, 0, 0, 1, true, 0};
    case 0x202:
        return PointerAction{PointerAction::Kind::Button, 0, 0, 1, false, 0};
    case 0x204:
    case 0x206:
        return PointerAction{PointerAction::Kind::Button, 0, 0, 3, true, 0};
    case 0x205:
        return PointerAction{PointerAction::Kind::Button, 0, 0, 3, false, 0};
    case 0x207:
    case 0x209:
        return PointerAction{PointerAction::Kind::Button, 0, 0, 2, true, 0};
    case 0x208:
        return PointerAction{PointerAction::Kind::Button, 0, 0, 2, false, 0};
    default:
        return {};
    }
}

} // namespace ultraview::host
