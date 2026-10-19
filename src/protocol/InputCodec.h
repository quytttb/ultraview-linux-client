#pragma once
//
// Input command codec: the ASCII commands observed in RC40 e3a/b66 (E56-E59).
//
// These payloads are carried inside relay frames with the UTF-16 flag set to
// false (ASCII), exactly like `input_control` in the reference, and the reference
// test `test_input_control.py` pins every byte.
//
#include <QByteArray>

#include <cstdint>

namespace ultraview::protocol {

enum class MouseButton { Left, Right, Middle };

class InputCodec {
public:
    /// "8|||512|||0|||x|||y"; coordinates are remote desktop pixels in -32768..32767.
    static QByteArray mouseMove(int x, int y);

    /// "8|||513"/"8|||514" (left), 516/517 (right), 519/520 (middle).
    static QByteArray mouseButton(MouseButton button, bool down);

    /// "8|||522|||<delta << 16>": the original extracts the signed high word of
    /// the packed wParam, so the value is shifted left by 16 and sent in decimal.
    static QByteArray mouseWheel(int delta);

    /// "8|||<event>|||<vk>" with 256/257 = WM_KEYDOWN/UP and 260/261 = the
    /// WM_SYSKEYDOWN/UP variants.
    static QByteArray keyboard(quint8 virtualKey, bool down, bool system = false);

    /// Screen-update acknowledgement sent after the frame has been *applied*
    /// (`ControlSession.acknowledge_screen` sends b"18").
    static QByteArray screenAcknowledged();

    /// Login acknowledgement follow-up (`ControlSession.receive` sends b"17").
    static QByteArray loginAcknowledged();

    /// Resolution/size follow-up for a kind-3 packet with a 4-byte field:
    /// `b"|||4" + ascii(Int32LE(field))`.
    static QByteArray sizeFollowUp(qint32 value);

    /// The exact event ids, exposed for tests and documentation.
    static constexpr quint16 kMouseMove = 512;
    static constexpr quint16 kMouseWheel = 522;
    static constexpr quint16 kKeyDown = 256;
    static constexpr quint16 kKeyUp = 257;
    static constexpr quint16 kSystemKeyDown = 260;
    static constexpr quint16 kSystemKeyUp = 261;
    static constexpr quint16 kLeftDown = 513;
    static constexpr quint16 kLeftUp = 514;
    static constexpr quint16 kRightDown = 516;
    static constexpr quint16 kRightUp = 517;
    static constexpr quint16 kMiddleDown = 519;
    static constexpr quint16 kMiddleUp = 520;
};

} // namespace ultraview::protocol
