#pragma once
//
// Wire framing primitives recovered from the Windows client (see docs/protocol.md).
//
// Two framings exist and both are modelled here exactly as the reference
// implementation does:
//
//  1. length framing            <Int32/UInt32 little endian length><body>
//     Used on the registration channel and on the relay channel.
//     `uv_reference.encode_transport_frame` writes an **Int32** (signed) length;
//     the receiver reads it as **UInt32**.  Both agree for every array length that
//     fits a managed array, so the native codec writes the same bytes and reads
//     them back with a *checked* limit.
//
//  2. relay framing             <0x00><flag byte><IPv4 as LE UInt32><port LE UInt16><payload>
//     `uv_reference.encode_relay_frame`.  The IPv4 helper packs the dotted quad
//     with the first octet most significant into a little-endian UInt32, i.e.
//     1.2.3.4 is transmitted as 01 04 03 02 01 -- reversed compared with the
//     dotted string; the fixture `0a000000000104030201d0076869` pins this down.
//
#include <QByteArray>
#include <QList>
#include <QString>

#include <cstdint>

namespace ultraview::protocol {

/// Maximum accepted frame body.  The reference transport defaults to 1 MiB
/// (`RegistrationTransport(max_frame=1024*1024)`) and refuses anything larger on
/// both directions; the native client keeps that default and allows the user to
/// raise it explicitly (settings) instead of silently buffering unbounded data.
inline constexpr quint32 kDefaultMaxFrameBytes = 1024u * 1024u;

/// Absolute ceiling, independent of configuration, so that a hostile peer cannot
/// make the client allocate arbitrary amounts of memory.
inline constexpr quint32 kAbsoluteMaxFrameBytes = 64u * 1024u * 1024u;

/// The HTTP tunnel preface advertises this literal content length.  It is copied
/// from the artifact and is **not** the length of anything the client sends
/// (`http_service_preface` documents it as "copied, not calculated"), so it must
/// never be "fixed" or computed.
inline constexpr qint64 kHttpPrefaceContentLength = 8589934590LL;

/// Decoded transport dispatch header (`decode_transport_body`, E37).
struct TransportBody {
    enum class Kind { Relay, Direct, Unknown };

    Kind kind = Kind::Unknown;
    /// Relay header byte 1, true when it was non-zero (`flag = body[1] != 0`).
    bool flag = false;
    /// Raw UInt32 address value; the reference deliberately keeps it unresolved.
    quint32 addressValue = 0;
    quint16 port = 0;
    QByteArray data;
};

/// Incremental length-framed reader, mirroring `uv_reference.FrameStream`.
///
/// Reference semantics that are preserved on purpose (they are pinned by tests):
///   * the HTTP header, when expected, is dropped up to and including the first
///     CRLFCRLF and no HTTP status validation happens here,
///   * the loop condition is `len(buffer) > 4`, so exactly four buffered bytes
///     stay pending even if they describe an empty frame (which is then produced
///     by the next byte),
///   * trailing incomplete frames stay buffered.
///
/// Deliberate divergence (documented in docs/compatibility.md): a frame longer
/// than the configured limit throws ErrorCode::Resource instead of growing the
/// buffer without bound.
class FrameStream {
public:
    explicit FrameStream(bool expectHttpHeader = false,
                         quint32 maxFrameBytes = kDefaultMaxFrameBytes);

    void setExpectHttpHeader(bool expect);
    bool expectingHttpHeader() const noexcept { return m_expectHttpHeader; }

    /// Drop the leading HTTP header if it is complete.  Returns the header bytes
    /// (including the terminating CRLFCRLF) or an empty array when still pending.
    /// Only meaningful while `expectingHttpHeader()` is true.
    QByteArray takeHttpHeader();

    /// Append `chunk` and return every complete frame it completed.
    QList<QByteArray> feed(const QByteArray &chunk);

    /// Bytes of the next frame that are already buffered but incomplete.
    qint64 pendingFrameBytes() const;
    qint64 bufferedBytes() const noexcept { return m_buffer.size(); }
    const QByteArray &buffer() const noexcept { return m_buffer; }
    bool hasPendingData() const noexcept { return !m_buffer.isEmpty(); }
    void clear();

    quint32 maxFrameBytes() const noexcept { return m_maxFrameBytes; }
    void setMaxFrameBytes(quint32 limit);

private:
    QByteArray m_buffer;
    bool m_expectHttpHeader = false;
    quint32 m_maxFrameBytes = kDefaultMaxFrameBytes;
};

/// `<Int32 LE length><body>`; refuses bodies that exceed the managed-array bound
/// exactly like `encode_transport_frame`.
QByteArray encodeTransportFrame(const QByteArray &body);

/// `<Int32 LE length><kind byte><payload>` (`encode_typed_transport_frame`).
QByteArray encodeTypedTransportFrame(const QByteArray &payload, quint8 kind);

/// Full relay frame: `<len><0x00><flag><address LE><port LE><payload>`.
/// `ipv4` must be a canonical dotted quad (the reference rejects anything else),
/// `port` a UInt16 and `flag` a byte.
QByteArray encodeRelayFrame(const QByteArray &payload, const QString &ipv4, quint16 port, quint8 flag);

/// Parse a transport dispatch header.  Throws ErrorCode::Protocol for truncated
/// relay headers; unknown kinds come back as Kind::Unknown (the reference returns
/// None instead of raising).
TransportBody decodeTransportBody(const QByteArray &body);

/// The recovered `POST /UVService/ HTTP/1.1` preface (`http_service_preface`).
QByteArray httpServicePreface(const QString &host, bool post = true);

/// Parse a canonical dotted quad into the little-endian wire UInt32 used by
/// `encode_relay_frame`.  Returns false when the string is not four decimal
/// octets in 0..255 (no string coercion, no Mod 256 -- see the reference notes).
bool parseCanonicalIpv4(const QString &ipv4, quint32 *addressValue);

/// Format a wire UInt32 back into a dotted quad (inverse of parseCanonicalIpv4).
QString formatIpv4(quint32 addressValue);

// ---------------------------------------------------------------------------
// R200 field arrays: `encode_byte_array_fields` / `decode_byte_array_fields`.
//
// Every session packet body is a sequence of `<UInt32 LE length><value>` fields.
// `None` and an empty byte array are indistinguishable on the wire (both are a
// zero length), which the reference documents explicitly.
// ---------------------------------------------------------------------------

QByteArray encodeByteArrayFields(const QList<QByteArray> &fields);

/// Parse a complete field payload.  Throws ErrorCode::Protocol on truncated
/// length/value data and on a field count that differs from `expectedCount`.
/// `expectedCount` of -1 means "any".
QList<QByteArray> decodeByteArrayFields(const QByteArray &payload, int expectedCount = -1);

} // namespace ultraview::protocol
