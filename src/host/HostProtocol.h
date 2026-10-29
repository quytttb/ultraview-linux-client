#pragma once
//
// Host-side protocol pieces: the 28-field registration, relay negotiation,
// viewer login verification and the screen/input messages a Windows
// UltraViewer viewer expects from a sender.
//
// The byte representation is ported from the pinned reference implementation
// (see docs/protocol.md and docs/host-mode.md).  Parsing is deliberately
// stricter than the recovered .NET code: every length, numeric field and
// coordinate is bounded before it reaches an allocation or the desktop.
//
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <optional>

namespace ultraview::host {

struct HostIdentity;

// ---------------------------------------------------------------------------
// Geometry and frames
// ---------------------------------------------------------------------------

/// Monitor bounds on the transmitted canvas.  The canvas is always normalised
/// to (0,0)..(width,height), so these values are non-negative.
struct HostMonitor {
    qint32 left = 0;
    qint32 top = 0;
    qint32 right = 0;
    qint32 bottom = 0;

    bool operator==(const HostMonitor &other) const = default;
};

/// One captured frame.  Pixels are bottom-up, tightly packed RGB555 (blue in
/// bits 0..4, green 5..9, red 10..14), exactly as sent by HostedScreenMessage.
struct HostFrame {
    int width = 0;
    int height = 0;
    QByteArray rgb555BottomUp;
    QVector<HostMonitor> monitors;
    QString layoutId;

    bool sameLayout(const HostFrame &other) const;
    bool isValid() const;
};

// ---------------------------------------------------------------------------
// Text commands
// ---------------------------------------------------------------------------

struct RegistrationReply {
    quint16 publicPort = 0;
    QString id;
    QString publicIp;
    qint32 status = 0;
    QStringList fields;
    QString pingList;      ///< field 6, default relay addresses
    QString pingHostNames; ///< field 9, default relay host names
    QString serverToken;   ///< field 10, opaque
};

/// Build the 28-field host command 3 in ANSI (printable ASCII).
QByteArray buildRegistrationCommand(const HostIdentity &identity);

/// Parse a plain `3:` reply.  Throws ErrorCode::Protocol on malformed fields.
RegistrationReply parseRegistrationReply(const QByteArray &payload);

struct RelayRequest {
    QString address;  ///< relay IP (canonical dotted quad)
    QString hostname; ///< relay DNS name used for TLS and the HTTP Host header
    QString tag;      ///< 17: correlation tag, empty for 11:
    QString hash;     ///< 17: correlation hash, empty for 11:
};

/// Parse a command-channel `11:` or `17:` relay request.  `defaultAddresses`
/// and `defaultHostNames` are the semicolon lists from the registration reply
/// (fields 6 and 9) and are used when the request leaves fields 3/4 empty.
std::optional<RelayRequest> parseRelayRequest(const QByteArray &payload,
                                              const QString &defaultAddresses,
                                              const QString &defaultHostNames);

struct RelayAddress {
    quint16 port = 0;
    QString address;
};

/// Parse the data relay's `0:port:ip` system reply.
std::optional<RelayAddress> parseRelayAddress(const QByteArray &payload);

/// The encrypted command-channel availability advertisement.
QByteArray relayAvailability(const RelayRequest &request, const RelayAddress &address);

/// Decode an inbound relay payload.  UTF-16LE (flag=1) is converted to UTF-8
/// and rejected when it contains control characters other than tab/LF/CR.
std::optional<QByteArray> peerPayload(const QByteArray &payload, bool unicode);

// ---------------------------------------------------------------------------
// Viewer login
// ---------------------------------------------------------------------------

struct LoginRequest {
    QStringList fields;
    qint32 screenWidth = 0;
    qint32 screenHeight = 0;
    qint32 controlMode = 0;
    qint32 displayColors = 0;
    qint32 displayQuality = 0;

    QString nonce() const { return fields.value(1); }
    QString claimedId() const { return fields.value(10); }
    QString displayName() const { return fields.value(11); }
    QString connectionHashKey() const { return fields.value(15); }
    QString clientVersion() const { return fields.value(21); }
};

/// Strict ASCII decode of `login:` with 23 fields.
std::optional<LoginRequest> parseLogin(const QByteArray &payload, QString *error = nullptr);

/// `field 2 == lowercase MD5(nonce + password)`, constant time.
bool verifyRandomPassword(const LoginRequest &login, const QString &password);

/// Optional fixed master-password proof: lowercase MD5(32 spaces + password).
QString customPasswordProof(const QString &password);
bool verifyCustomProof(const LoginRequest &login, const QString &proof);

/// Five wrong passwords in a row lock new attempts for five minutes.
class LoginThrottle {
public:
    static constexpr quint32 kMaxFailures = 5;
    static constexpr qint64 kLockoutMs = 5 * 60 * 1000;

    bool isLocked(qint64 nowMs) const;
    /// Returns true when this failure starts the lockout.
    bool recordFailure(qint64 nowMs);
    void recordSuccess();
    quint32 failures() const { return m_failures; }

private:
    quint32 m_failures = 0;
    qint64 m_lockedUntilMs = -1;
};

// ---------------------------------------------------------------------------
// Input messages
// ---------------------------------------------------------------------------

enum class HostInputType { Mouse, Keyboard };

struct HostInputEvent {
    HostInputType type = HostInputType::Mouse;
    // Mouse: Windows message, viewer-canvas coordinates, wheel delta.
    quint32 message = 0;
    qint32 x = 0;
    qint32 y = 0;
    qint16 wheel = 0;
    // Keyboard: 1..4, virtual key, set-1 scan code, UTF-16 unit.
    quint8 kind = 0;
    quint8 virtualKey = 0;
    quint16 scanCode = 0;
    quint16 character = 0;
};

/// Decode an inbound `8|||...` mouse message or the 11-byte binary keyboard
/// message.  Malformed or out-of-range payloads return nullopt.
std::optional<HostInputEvent> decodeInput(const QByteArray &payload);

// ---------------------------------------------------------------------------
// Screen messages
// ---------------------------------------------------------------------------

constexpr qint32 kFirstFrameNumber = -2147483646;
constexpr int kMaxDimension = 8192;
constexpr int kMaxPayload = 16 * 1024 * 1024;

/// Command 0 initialization.  `monitors` may be empty for a single display;
/// the legacy fallback rectangle is then the whole canvas.
QByteArray initialization(int width, int height, const QString &version,
                          const QVector<HostMonitor> &monitors);

/// Command 1, one full-screen RGB555 rectangle with zstd level 1.
QByteArray fullFrame(const HostFrame &frame, qint32 sequence);

/// Command 20 reply to the viewer's ASCII keepalive.
QByteArray keepaliveReply();

/// Command 49 permission update: flag 0 allows control, 1 means view-only.
QByteArray controlPermission(bool allowControl);

/// A single-command multipart message (`22`, `27`, ...).
QByteArray singleCommand(quint8 command);

/// Packed little-endian UInt32 field arrays.
QByteArray encodeMultipart(const QList<QByteArray> &parts);

/// Convert top-down BGRx/BGRA (or RGBx/RGBA) pixels to bottom-up RGB555.
QByteArray rgb555FromBgrx(const uchar *source, int width, int height, int stride,
                          bool rgbOrder = false);

/// Box-filter a packed bottom-up RGB555 image.
QByteArray scaleRgb555(const QByteArray &source, int sourceWidth, int sourceHeight,
                       int width, int height);

} // namespace ultraview::host
