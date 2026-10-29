#pragma once
//
// Peer application commands that are plain text or multipart messages rather
// than screen updates: chat, typing indicators, session actions, quality,
// wallpaper, audio toggling and cursor tracking.
//
// All of them travel inside relay frames.  The relay flag is part of the command
// definition (UTF-16LE flag 1 for text commands, ASCII flag 0 for the numeric
// ones) and is therefore returned with the payload instead of being guessed by
// the caller.
//
#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace ultraview::protocol {

/// One relay payload plus the flag byte the relay frame must carry.
struct CommandPayload {
    QByteArray payload;
    quint8 flag = 0;
};

class ControlCommands {
public:
    // --- viewer -> host ----------------------------------------------------
    /// In-session chat (`12|||<text>`, UTF-16LE).  Refuses `|||` and text longer
    /// than 32768 UTF-16 bytes, the documented host limit.
    static CommandPayload chat(const QString &text);
    static CommandPayload typing(bool started);
    /// Secure attention sequence (`22`), remote reboot (`40`) and remote lock
    /// (`53`); all three are bare UTF-16LE action codes.
    static CommandPayload ctrlAltDel();
    static CommandPayload reboot();
    static CommandPayload lockWorkstation();
    /// `46|||-1` starts the WASAPI loopback stream, `46|||0` stops it.
    static CommandPayload audio(bool enabled);
    /// `23|||-1` shows the partner wallpaper, `23|||0` blanks it.
    static CommandPayload wallpaper(bool shown);
    /// `31|||<value>`: 30 = speed, 45 = balanced, 70 = quality.
    static CommandPayload quality(int value);
    /// `32|||2`: force the RGB555 color mode.
    static CommandPayload colorModeRgb555();
    /// `28|||1` enables remote pointer tracking, `28|||0` disables it.  The host
    /// answers with command 28 position updates and command 3 shape selections.
    static CommandPayload cursorTracking(bool enabled);
    /// `4|||<hash>`: ask for a cursor shape that is not in the local cache.
    static CommandPayload cursorShapeRequest(qint32 hash);
    /// `19` announces a viewer-initiated teardown, `20` is the 10 s heartbeat.
    static CommandPayload disconnectNotice();
    static CommandPayload heartbeat();

    static constexpr int kMaxChatCodeUnits = 32768 / 2;

    // --- host -> viewer ----------------------------------------------------
    /// Multipart command 12; the text is in field 1 (UTF-16LE).
    static bool isChat(const QList<QByteArray> &fields);
    static std::optional<QString> parseChat(const QList<QByteArray> &fields);
    /// Multipart commands 29 (started) / 30 (stopped).
    static bool isTyping(const QList<QByteArray> &fields, bool *started);
    /// Byte payload of command 49: 0 = full control, 1 = view only.
    static std::optional<bool> parseViewOnly(const QList<QByteArray> &fields);
};

} // namespace ultraview::protocol
