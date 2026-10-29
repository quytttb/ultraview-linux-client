#pragma once
//
// File transfer messages (commands 13, 14, 15, 16 and 21).
//
// The viewer sends delimited ASCII with a raw chunk appended; the host answers
// with multipart payloads whose numeric fields are IEEE-754 doubles in little
// endian byte order.  Both directions are modelled here; the state machine and
// the disk I/O live in `transfer::FileTransferService`.
//
//   offer   13|||<transferID>|||<name>|||<size>      multipart: id, name, double
//   accept  14|||<transferID>|||1                    multipart: id, int32 1
//   chunk   15|||<transferID>|||<offset>|||<bytes>   multipart: id, double, data
//   cancel  16|||<transferID>                        multipart: id
//   ack     21|||<transferID>                        multipart: id
//
// One chunk is acknowledged before the next one is sent, so the protocol has a
// one-chunk window; `FileTransferService` implements that pacing.
//
#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace ultraview::protocol {

struct FileOfferMessage {
    QString transferId;
    QString name;
    qint64 size = 0;
};

struct FileChunkMessage {
    QString transferId;
    qint64 offset = 0;
    QByteArray data;
};

struct FileTransferMessage {
    enum class Kind { None, Offer, Accept, Chunk, Cancel, Ack };

    Kind kind = Kind::None;
    FileOfferMessage offer;
    FileChunkMessage chunk;
    QString transferId;
    bool accepted = false;
};

class FileTransferCodec {
public:
    /// Documented chunk payload limit.
    static constexpr int kMaxChunkBytes = 32000;
    /// Documented file size ceiling (`999999999999999`).
    static constexpr qint64 kMaxFileBytes = 999999999999999LL;
    /// Largest chunk this client accepts from a host (the documented limit plus a
    /// safety margin for a host that rounded up its buffer).
    static constexpr int kMaxInboundChunkBytes = 65536;

    /// Transfer IDs are generated locally; the host echoes them back.
    static QString newTransferId();

    /// A file name is only accepted when it is a plain name: no directory
    /// separators, no `..`, no control characters and no protocol delimiter.
    static bool isSafeFileName(const QString &name);

    // --- viewer -> host (all relay flag 0) ---------------------------------
    static QByteArray offer(const QString &transferId, const QString &name, qint64 size);
    static QByteArray accept(const QString &transferId, bool accepted);
    static QByteArray chunk(const QString &transferId, qint64 offset, const QByteArray &data);
    static QByteArray cancel(const QString &transferId);
    static QByteArray acknowledge(const QString &transferId);

    // --- host -> viewer ----------------------------------------------------
    /// Parse a multipart message.  Throws UvException when a command is present
    /// but its fields are malformed; returns Kind::None for other commands.
    static FileTransferMessage parse(const QList<QByteArray> &fields);

    /// Parse the delimited ASCII form (`13|||id|||name|||size`), which a host may
    /// use instead of multipart.  Returns Kind::None when the payload is not a
    /// file transfer command.
    static FileTransferMessage parseText(const QByteArray &payload);

    /// True when the first field starts with one of the file transfer commands.
    static bool isTransferCommand(const QList<QByteArray> &fields);

    /// Little-endian IEEE-754 helpers, exposed for byte-level tests.
    static QByteArray encodeDouble(qint64 value);
    static bool decodeDouble(const QByteArray &bytes, qint64 *value);
};

} // namespace ultraview::protocol
