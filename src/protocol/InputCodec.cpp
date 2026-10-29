#include "protocol/InputCodec.h"

#include <QCoreApplication>
#include "core/ErrorTypes.h"

namespace ultraview::protocol {
namespace {

QString checkedInteger(int value, int low, int high)
{
    if (value < low || value > high)
        throwError(ErrorCode::Configuration, QCoreApplication::translate("InputCodec", "input value outside supported range"),
                   QString::number(value));
    return QString::number(value);
}

quint16 buttonEvent(MouseButton button, bool down)
{
    switch (button) {
    case MouseButton::Left:
        return down ? InputCodec::kLeftDown : InputCodec::kLeftUp;
    case MouseButton::Right:
        return down ? InputCodec::kRightDown : InputCodec::kRightUp;
    case MouseButton::Middle:
        return down ? InputCodec::kMiddleDown : InputCodec::kMiddleUp;
    }
    throwError(ErrorCode::Internal, QCoreApplication::translate("InputCodec", "unsupported mouse button"));
}

QByteArray command8(const QString &suffix)
{
    return QByteArrayLiteral("8|||") + suffix.toLatin1();
}

} // namespace

QByteArray InputCodec::mouseMove(int x, int y)
{
    const QString message = QStringLiteral("%1|||0|||%2|||%3")
                                .arg(kMouseMove)
                                .arg(checkedInteger(x, -32768, 32767), checkedInteger(y, -32768, 32767));
    return command8(message);
}

QByteArray InputCodec::mouseButton(MouseButton button, bool down)
{
    return command8(QString::number(buttonEvent(button, down)));
}

QByteArray InputCodec::mouseWheel(int delta)
{
    const QString text = checkedInteger(delta, -32768, 32767);
    const qint32 shifted = static_cast<qint32>(qint64{text.toInt()} * 65536);
    return command8(QStringLiteral("%1|||%2").arg(kMouseWheel).arg(shifted));
}

QByteArray InputCodec::keyboard(quint8 virtualKey, bool down, bool system)
{
    const quint16 event = system ? (down ? kSystemKeyDown : kSystemKeyUp)
                                 : (down ? kKeyDown : kKeyUp);
    return command8(QStringLiteral("%1|||%2").arg(event).arg(static_cast<uint>(virtualKey)));
}

QByteArray InputCodec::screenAcknowledged()
{
    return QByteArrayLiteral("18");
}

QByteArray InputCodec::loginAcknowledged()
{
    return QByteArrayLiteral("17");
}

QByteArray InputCodec::sizeFollowUp(qint32 value)
{
    return QByteArrayLiteral("|||4") + QByteArray::number(value);
}

} // namespace ultraview::protocol
