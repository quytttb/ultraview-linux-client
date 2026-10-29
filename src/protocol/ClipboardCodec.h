#pragma once
//
// Text clipboard synchronization (commands 5 and 33).
//
// Two encodings are in use, both carried inside relay frames:
//
//   legacy (command 5)   UTF-16LE text `5|||<text>`, relay flag 1
//   modern (command 33)  ASCII `33|||<utf16_byte_count>|||` followed by a
//                        dictionary-free Zstandard frame of UTF-16LE text,
//                        relay flag 0 on upload and a multipart payload
//                        [command byte][uint32 length][zstd] on download
//
// The empty string is special: the documented host rejects it in the modern
// encoding (an empty frame has no content), so `buildUpload()` falls back to the
// legacy form for empty text, exactly like the reference plugin.
//
// Delimiters: `|||` inside the text would corrupt the legacy form, so legacy
// uploads refuse it instead of sending a message the host would split.
//
#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace ultraview::protocol {

class ClipboardCodec {
public:
    /// Ceiling for one clipboard message in either direction.  The host caps its
    /// own text far below this; the limit only bounds what this client accepts.
    static constexpr qint64 kMaxClipboardBytes = 1024 * 1024;

    /// Relay flag that must be used with a payload (1 = UTF-16LE text).
    struct Upload {
        QByteArray payload;
        quint8 flag = 0;
    };

    /// Command 33 when `text` is not empty, command 5 when it is.
    static Upload buildUpload(const QString &text);

    /// Always the legacy command 5 form.
    static Upload buildLegacy(const QString &text);

    /// Always the modern command 33 form.  Throws when `text` is empty.
    static Upload buildModern(const QString &text);

    /// Decode an inbound legacy text payload (relay flag 1).  Returns nothing when
    /// the payload is not a clipboard message (wrong command, unreadable units).
    static std::optional<QString> parseLegacy(const QByteArray &payload);

    /// Decode an inbound multipart command 33 payload.  Returns nothing when the
    /// payload is not a clipboard message.  Throws UvException on a command 33
    /// message whose length field or compression is malformed, because silently
    /// dropping text the partner believed it sent is worse than one notice.
    static std::optional<QString> parseModern(const QList<QByteArray> &fields);

    /// True when `fields` starts with the command 33 discriminator.
    static bool isModernCommand(const QList<QByteArray> &fields);

    /// UTF-16LE helpers shared with the other text commands.
    static QByteArray toUtf16(const QString &text);
    static std::optional<QString> fromUtf16(const QByteArray &bytes);

    /// ASCII header of the modern upload, without the compressed body.
    /// Exposed for byte-level tests.
    static QByteArray modernHeader(qint64 utf16ByteCount);

    static constexpr quint8 kLegacyCommand = 5;
    static constexpr quint8 kModernCommand = 33;
};

} // namespace ultraview::protocol
