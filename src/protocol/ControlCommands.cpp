#include "protocol/ControlCommands.h"

#include "core/ErrorTypes.h"
#include "protocol/ClipboardCodec.h"

namespace ultraview::protocol {
namespace {

constexpr char kDelimiter[] = "|||";

/// UTF-16LE payload of a bare command code such as `22` or `46|||-1`.
CommandPayload utf16Command(const QString &text)
{
    CommandPayload payload;
    payload.payload = ClipboardCodec::toUtf16(text);
    payload.flag = 1;
    return payload;
}

/// ASCII payload for the numeric commands.
CommandPayload asciiCommand(const QString &text)
{
    CommandPayload payload;
    payload.payload = text.toLatin1();
    payload.flag = 0;
    return payload;
}

/// Command discriminator of a multipart payload.  A command may be sent as one
/// byte or as a little-endian 16-bit value (`[0x0c]` or `[0x0c, 0x00]`).
bool commandOf(const QList<QByteArray> &fields, int *command)
{
    if (fields.isEmpty() || fields.first().isEmpty())
        return false;
    const QByteArray &first = fields.first();
    if (first.size() > 2)
        return false;
    int value = 0;
    for (int index = static_cast<int>(first.size()) - 1; index >= 0; --index)
        value = (value << 8) | static_cast<quint8>(first.at(index));
    *command = value;
    return true;
}

} // namespace

CommandPayload ControlCommands::chat(const QString &text)
{
    if (text.contains(QLatin1String(kDelimiter))) {
        throwError(ErrorCode::UnsupportedOperation,
                   QStringLiteral("chat text contains the protocol delimiter"),
                   QStringLiteral("the host splits chat messages on \"|||\""));
    }
    if (text.size() > kMaxChatCodeUnits) {
        throwError(ErrorCode::Resource, QStringLiteral("chat message is too long"),
                   QStringLiteral("%1 code units, limit %2").arg(text.size()).arg(kMaxChatCodeUnits));
    }
    return utf16Command(QString::fromLatin1("12") + QLatin1String(kDelimiter) + text);
}

CommandPayload ControlCommands::typing(bool started)
{
    return utf16Command(started ? QStringLiteral("29") : QStringLiteral("30"));
}

CommandPayload ControlCommands::ctrlAltDel()
{
    return utf16Command(QStringLiteral("22"));
}

CommandPayload ControlCommands::reboot()
{
    return utf16Command(QStringLiteral("40"));
}

CommandPayload ControlCommands::lockWorkstation()
{
    return utf16Command(QStringLiteral("53"));
}

CommandPayload ControlCommands::audio(bool enabled)
{
    return utf16Command(QString::fromLatin1("46") + QLatin1String(kDelimiter)
                        + (enabled ? QStringLiteral("-1") : QStringLiteral("0")));
}

CommandPayload ControlCommands::wallpaper(bool shown)
{
    return utf16Command(QString::fromLatin1("23") + QLatin1String(kDelimiter)
                        + (shown ? QStringLiteral("-1") : QStringLiteral("0")));
}

CommandPayload ControlCommands::quality(int value)
{
    // The three presets the host documents; anything else is a caller bug, not
    // something to forward blindly to the partner's encoder.
    if (value != 30 && value != 45 && value != 70) {
        throwError(ErrorCode::Configuration, QStringLiteral("unsupported display quality"),
                   QStringLiteral("expected 30, 45 or 70"));
    }
    return asciiCommand(QString::fromLatin1("31") + QLatin1String(kDelimiter)
                        + QString::number(value));
}

CommandPayload ControlCommands::colorModeRgb555()
{
    return asciiCommand(QString::fromLatin1("32") + QLatin1String(kDelimiter) + QStringLiteral("2"));
}

CommandPayload ControlCommands::cursorTracking(bool enabled)
{
    return asciiCommand(QString::fromLatin1("28") + QLatin1String(kDelimiter)
                        + (enabled ? QStringLiteral("1") : QStringLiteral("0")));
}

CommandPayload ControlCommands::cursorShapeRequest(qint32 hash)
{
    return asciiCommand(QString::fromLatin1("4") + QLatin1String(kDelimiter)
                        + QString::number(hash));
}

CommandPayload ControlCommands::disconnectNotice()
{
    return asciiCommand(QStringLiteral("19"));
}

CommandPayload ControlCommands::heartbeat()
{
    return asciiCommand(QStringLiteral("20"));
}

bool ControlCommands::isChat(const QList<QByteArray> &fields)
{
    int command = 0;
    return commandOf(fields, &command) && command == 12;
}

std::optional<QString> ControlCommands::parseChat(const QList<QByteArray> &fields)
{
    int command = 0;
    if (!commandOf(fields, &command) || command != 12)
        return std::nullopt;
    if (fields.size() < 2)
        return QString();
    return ClipboardCodec::fromUtf16(fields.at(1));
}

bool ControlCommands::isTyping(const QList<QByteArray> &fields, bool *started)
{
    int command = 0;
    if (!commandOf(fields, &command) || (command != 29 && command != 30))
        return false;
    if (started != nullptr)
        *started = command == 29;
    return true;
}

std::optional<bool> ControlCommands::parseViewOnly(const QList<QByteArray> &fields)
{
    int command = 0;
    if (!commandOf(fields, &command) || command != 49)
        return std::nullopt;
    if (fields.size() < 2 || fields.at(1).isEmpty())
        return std::nullopt;
    return static_cast<quint8>(fields.at(1).at(0)) != 0;
}

} // namespace ultraview::protocol
