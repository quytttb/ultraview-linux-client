//
// Parity tests for protocol/InputCodec plus the client-side Qt key -> Windows
// virtual-key glue (input/KeyboardMapper).
//
// The input payloads are pinned byte-exactly by the reference fixture
// `input_commands` (recovered from RC40 e3a/b66, E56-E59 and
// `reference/test_input_control.py`).  The key mapping table is *not* part of the
// wire format; it is exercised here so that every mapping this client can send is
// covered by its own expectation.
//
#include "TestSupport.h"

#include "core/ErrorTypes.h"
#include "input/KeyboardMapper.h"
#include "input/InputController.h"
#include "protocol/InputCodec.h"

#include <QtTest>

using namespace ultraview;
using namespace ultraview::input;
using namespace ultraview::protocol;

class TestKeyboardMapper : public QObject {
    Q_OBJECT

private slots:
    void inputCommands();
    void inputCommandRanges();
    void acknowledgementPayloads();
    void namedKeys();
    void lettersDigitsAndFunctionKeys();
    void modifierSystemFlag();
    void oemKeys();
    void keypadKeys();
    void unmappedKeys();
    void virtualKeyNames();
    void mappingRoundTripThroughInputCodec();
    void releasesTheOriginalMapping();
};

void TestKeyboardMapper::inputCommands()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("input_commands"));
    QVERIFY(!cases.isEmpty());
    int moves = 0;
    int buttons = 0;
    int wheels = 0;
    int keys = 0;
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        const QByteArray expected = uvtest::Fixtures::entryBytes(entry, QStringLiteral("payload"));
        const QString op = entry.value(QStringLiteral("op")).toString();
        if (op == QLatin1String("move")) {
            QCOMPARE(InputCodec::mouseMove(entry.value(QStringLiteral("x")).toInt(),
                                           entry.value(QStringLiteral("y")).toInt()),
                     expected);
            ++moves;
        } else if (op == QLatin1String("button")) {
            const QString button = entry.value(QStringLiteral("button")).toString();
            const MouseButton code = button == QLatin1String("left")    ? MouseButton::Left
                : button == QLatin1String("right")                     ? MouseButton::Right
                                                                       : MouseButton::Middle;
            QCOMPARE(InputCodec::mouseButton(code, entry.value(QStringLiteral("down")).toBool()), expected);
            ++buttons;
        } else if (op == QLatin1String("wheel")) {
            QCOMPARE(InputCodec::mouseWheel(entry.value(QStringLiteral("delta")).toInt()), expected);
            ++wheels;
        } else if (op == QLatin1String("key")) {
            QCOMPARE(InputCodec::keyboard(static_cast<quint8>(entry.value(QStringLiteral("vk")).toInt()),
                                          entry.value(QStringLiteral("down")).toBool(),
                                          entry.value(QStringLiteral("system")).toBool()),
                     expected);
            ++keys;
        } else {
            QFAIL("unknown input command in the fixture");
        }
    }
    QCOMPARE(moves, 4);
    QCOMPARE(buttons, 6);
    QCOMPARE(wheels, 4);
    QCOMPARE(keys, 8);
}

void TestKeyboardMapper::inputCommandRanges()
{
    // Coordinates outside the modelled Int16 range are refused instead of being
    // truncated into a different remote position.
    QVERIFY_EXCEPTION_THROWN(InputCodec::mouseMove(32768, 0), UvException);
    QVERIFY_EXCEPTION_THROWN(InputCodec::mouseMove(0, -32769), UvException);
    // The wheel value is shifted left by 16, so only the Int16 range can survive.
    QVERIFY_EXCEPTION_THROWN(InputCodec::mouseWheel(32768), UvException);
    QVERIFY_EXCEPTION_THROWN(InputCodec::mouseWheel(-32769), UvException);
    QCOMPARE(InputCodec::mouseMove(32767, -32768), QByteArrayLiteral("8|||512|||0|||32767|||-32768"));
    QCOMPARE(InputCodec::mouseWheel(120), QByteArrayLiteral("8|||522|||7864320"));
}

void TestKeyboardMapper::acknowledgementPayloads()
{
    QCOMPARE(InputCodec::loginAcknowledged(), QByteArrayLiteral("17"));
    QCOMPARE(InputCodec::screenAcknowledged(), QByteArrayLiteral("18"));
    QCOMPARE(InputCodec::sizeFollowUp(1), QByteArrayLiteral("|||4") + QByteArrayLiteral("1"));
    // The reference reads the field as Int32LE and prints it in decimal.
    QCOMPARE(InputCodec::sizeFollowUp(0x12345678), QByteArrayLiteral("|||4") + QByteArrayLiteral("305419896"));
    QCOMPARE(InputCodec::sizeFollowUp(-1), QByteArrayLiteral("|||4") + QByteArrayLiteral("-1"));
    QCOMPARE(InputCodec::kMouseMove, 512);
    QCOMPARE(InputCodec::kMouseWheel, 522);
    QCOMPARE(InputCodec::kKeyDown, 256);
    QCOMPARE(InputCodec::kKeyUp, 257);
    QCOMPARE(InputCodec::kSystemKeyDown, 260);
    QCOMPARE(InputCodec::kSystemKeyUp, 261);
}

void TestKeyboardMapper::namedKeys()
{
    const auto expect = [](int key, quint8 expected, Qt::KeyboardModifiers modifiers = Qt::NoModifier) -> bool {
        const std::optional<KeyMapping> mapping = KeyboardMapper::mapQtKey(key, modifiers);
        if (!mapping.has_value())
            return false;
        return mapping->virtualKey == expected;
    };
    QVERIFY(expect(Qt::Key_Escape, 0x1B));
    QVERIFY(expect(Qt::Key_Tab, 0x09));
    QVERIFY(expect(Qt::Key_Backspace, 0x08));
    QVERIFY(expect(Qt::Key_Return, 0x0D));
    QVERIFY(expect(Qt::Key_Enter, 0x0D));
    QVERIFY(expect(Qt::Key_Insert, 0x2D));
    QVERIFY(expect(Qt::Key_Delete, 0x2E));
    QVERIFY(expect(Qt::Key_Home, 0x24));
    QVERIFY(expect(Qt::Key_End, 0x23));
    QVERIFY(expect(Qt::Key_PageUp, 0x21));
    QVERIFY(expect(Qt::Key_PageDown, 0x22));
    QVERIFY(expect(Qt::Key_Left, 0x25));
    QVERIFY(expect(Qt::Key_Up, 0x26));
    QVERIFY(expect(Qt::Key_Right, 0x27));
    QVERIFY(expect(Qt::Key_Down, 0x28));
    QVERIFY(expect(Qt::Key_Space, 0x20));
    QVERIFY(expect(Qt::Key_Shift, 0x10));
    QVERIFY(expect(Qt::Key_Control, 0x11));
    QVERIFY(expect(Qt::Key_Alt, 0x12));
    QVERIFY(expect(Qt::Key_CapsLock, 0x14));
    QVERIFY(expect(Qt::Key_NumLock, 0x90));
    QVERIFY(expect(Qt::Key_ScrollLock, 0x91));
    QVERIFY(expect(Qt::Key_Print, 0x2C));
    QVERIFY(expect(Qt::Key_Pause, 0x13));
    QVERIFY(expect(Qt::Key_Menu, 0x5D));
    QVERIFY(expect(Qt::Key_Meta, 0x5B));
}

void TestKeyboardMapper::lettersDigitsAndFunctionKeys()
{
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_A, Qt::NoModifier)->virtualKey, 0x41);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_Z, Qt::NoModifier)->virtualKey, 0x5A);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_Q, Qt::ShiftModifier, QStringLiteral("Q"))->virtualKey, 0x51);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_0, Qt::NoModifier)->virtualKey, 0x30);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_9, Qt::NoModifier)->virtualKey, 0x39);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_F1, Qt::NoModifier)->virtualKey, 0x70);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_F12, Qt::NoModifier)->virtualKey, 0x7B);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_F24, Qt::NoModifier)->virtualKey, 0x87);
}

void TestKeyboardMapper::modifierSystemFlag()
{
    // Windows turns Alt combinations into WM_SYSKEYDOWN/UP; the recovered codec
    // exposes that through its `system` flag (260/261 instead of 256/257).
    QVERIFY(KeyboardMapper::mapQtKey(Qt::Key_Alt, Qt::NoModifier)->system);
    QVERIFY(KeyboardMapper::mapQtKey(Qt::Key_F4, Qt::AltModifier)->system);
    QVERIFY(!KeyboardMapper::mapQtKey(Qt::Key_F4, Qt::NoModifier)->system);
    QVERIFY(!KeyboardMapper::mapQtKey(Qt::Key_A, Qt::ControlModifier)->system);
    QVERIFY(!KeyboardMapper::mapQtKey(Qt::Key_A, Qt::ShiftModifier)->system);
    // The flag has to reach the wire as 260/261.
    QCOMPARE(InputCodec::keyboard(0x73, true, KeyboardMapper::mapQtKey(Qt::Key_F4, Qt::AltModifier)->system),
             QByteArrayLiteral("8|||260|||115"));
    QCOMPARE(InputCodec::keyboard(0x73, true, KeyboardMapper::mapQtKey(Qt::Key_F4, Qt::NoModifier)->system),
             QByteArrayLiteral("8|||256|||115"));
}

void TestKeyboardMapper::oemKeys()
{
    // Layout dependent printable keys use the text the platform produced.
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_Semicolon, Qt::NoModifier, QStringLiteral(";"))->virtualKey, 0xBA);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_Equal, Qt::NoModifier, QStringLiteral("="))->virtualKey, 0xBB);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_Comma, Qt::NoModifier, QStringLiteral(","))->virtualKey, 0xBC);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_Minus, Qt::NoModifier, QStringLiteral("-"))->virtualKey, 0xBD);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_Period, Qt::NoModifier, QStringLiteral("."))->virtualKey, 0xBE);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_Slash, Qt::NoModifier, QStringLiteral("/"))->virtualKey, 0xBF);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_QuoteLeft, Qt::NoModifier, QStringLiteral("`"))->virtualKey, 0xC0);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_BracketLeft, Qt::NoModifier, QStringLiteral("["))->virtualKey, 0xDB);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_Backslash, Qt::NoModifier, QStringLiteral("\\"))->virtualKey, 0xDC);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_BracketRight, Qt::NoModifier, QStringLiteral("]"))->virtualKey, 0xDD);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_Apostrophe, Qt::NoModifier, QStringLiteral("'"))->virtualKey, 0xDE);
    // Shifted variants map to the same physical key, like Windows does.
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_Colon, Qt::ShiftModifier, QStringLiteral(":"))->virtualKey, 0xBA);
    QVERIFY(!KeyboardMapper::mapQtKey(Qt::Key_ParenLeft, Qt::ShiftModifier, QStringLiteral("(")).has_value());
}

void TestKeyboardMapper::keypadKeys()
{
    const Qt::KeyboardModifiers keypad = Qt::KeypadModifier;
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_0, keypad)->virtualKey, 0x60);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_9, keypad)->virtualKey, 0x69);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_Asterisk, keypad)->virtualKey, 0x6A);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_Plus, keypad)->virtualKey, 0x6B);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_Minus, keypad)->virtualKey, 0x6D);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_Period, keypad)->virtualKey, 0x6E);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_Slash, keypad)->virtualKey, 0x6F);
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_Enter, keypad)->virtualKey, 0x0D);
    // Without the keypad modifier the main row mapping applies.
    QCOMPARE(KeyboardMapper::mapQtKey(Qt::Key_5, Qt::NoModifier)->virtualKey, 0x35);
}

void TestKeyboardMapper::unmappedKeys()
{
    // Media/unknown keys are reported as unmapped instead of being sent as 0, so
    // the UI can tell the user which key did not reach the partner.
    QVERIFY(!KeyboardMapper::mapQtKey(Qt::Key_unknown, Qt::NoModifier).has_value());
    QVERIFY(!KeyboardMapper::mapQtKey(Qt::Key_Dead_Grave, Qt::NoModifier).has_value());
    QVERIFY(!KeyboardMapper::mapQtKey(0x0, Qt::NoModifier).has_value());
}

void TestKeyboardMapper::virtualKeyNames()
{
    QCOMPARE(KeyboardMapper::virtualKeyName(0x0D), QStringLiteral("Enter"));
    QCOMPARE(KeyboardMapper::virtualKeyName(0x41), QStringLiteral("Key A"));
    QCOMPARE(KeyboardMapper::virtualKeyName(0x30), QStringLiteral("Digit 0"));
    QCOMPARE(KeyboardMapper::virtualKeyName(0x70), QStringLiteral("F1"));
    QCOMPARE(KeyboardMapper::virtualKeyName(0x87), QStringLiteral("F24"));
    QCOMPARE(KeyboardMapper::virtualKeyName(0x64), QStringLiteral("Numpad 4"));
    QCOMPARE(KeyboardMapper::virtualKeyName(0xBA), QStringLiteral(";"));
    QVERIFY(KeyboardMapper::virtualKeyName(0xFE).startsWith(QStringLiteral("VK 0x")));
}

void TestKeyboardMapper::mappingRoundTripThroughInputCodec()
{
    // A realistic key press: Alt+F4 has to leave this client as the exact ASCII
    // command the partner expects, with the system variant of the key event.
    const std::optional<KeyMapping> mapping = KeyboardMapper::mapQtKey(Qt::Key_F4, Qt::AltModifier);
    QVERIFY(mapping.has_value());
    const QByteArray down = InputCodec::keyboard(mapping->virtualKey, true, mapping->system);
    const QByteArray up = InputCodec::keyboard(mapping->virtualKey, false, mapping->system);
    QCOMPARE(down, QByteArrayLiteral("8|||260|||115"));
    QCOMPARE(up, QByteArrayLiteral("8|||261|||115"));
    for (const QByteArray &payload : {down, up}) {
        // Input commands travel as plain ASCII inside a relay frame (flag 0).
        for (const char unit : payload)
            QVERIFY(static_cast<quint8>(unit) >= 0x20 && static_cast<quint8>(unit) < 0x7F);
    }
}

void TestKeyboardMapper::releasesTheOriginalMapping()
{
    InputController input;
    QSignalSpy commands(&input, &InputController::commandReady);
    QVERIFY(input.keyPress(Qt::Key_F4, Qt::AltModifier, QString(), false));
    QVERIFY(input.keyRelease(Qt::Key_F4, Qt::NoModifier));
    QCOMPARE(commands.last().first().toByteArray(), QByteArrayLiteral("8|||261|||115"));
    QVERIFY(input.keyPress(Qt::Key_unknown, Qt::NoModifier, QStringLiteral(";"), false));
    input.releaseAll();
    QCOMPARE(commands.last().first().toByteArray(), QByteArrayLiteral("8|||257|||186"));
    QVERIFY(!input.anyInputHeld());
    const auto count = commands.size();
    input.setEnabled(false);
    input.press(Qt::LeftButton);
    input.moveTo(1, 2);
    QCOMPARE(commands.size(), count);
}

QTEST_MAIN(TestKeyboardMapper)
#include "tst_keyboard_mapper.moc"
