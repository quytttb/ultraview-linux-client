#pragma once
//
// Wire codec for the peer-to-peer (direct) transport variant.
//
// IMPORTANT SCOPE NOTE: the recovered code contains a P2P path (AES-256-CBC with
// the key derived from a generated password, plus a boolean "unicode" flag in
// front of each body).  The reference implementation models its *byte
// representation* only -- `encode_p2p_frame`, `decode_p2p_frame_body`,
// `derive_p2p_aes_key`, `encrypt_p2p_aes_body` -- and deliberately never performs
// a P2P handshake: `control_session.ControlSession` uses the relay path only.
//
// This file therefore ports the verified byte format (fixtures:
// `p2p.frames`, `p2p.decrypt_body`, matching `reference/test_p2p_wire.py`) but is
// NOT wired into the session state machine.  The client reports P2P as
// unsupported instead of pretending to negotiate it; see docs/compatibility.md.
//
#include <QByteArray>

#include <cstdint>
#include <optional>

namespace ultraview::protocol {

class P2pCodec {
public:
    /// `derive_p2p_aes_key`: MD5(ASCII(password)) copied at offsets 0 and 15 into
    /// a zeroed 32-byte key.  ASCII-only, like the recovered generated alphabet.
    static QByteArray deriveAesKey(const QString &password);

    /// `encrypt_p2p_aes_body`: IV(16) || AES-256-CBC-PKCS7(plaintext).
    /// Omit `iv` for fresh random bytes; pass one for reproducible fixtures.
    static QByteArray encryptBody(const QByteArray &plaintext, const QString &password,
                                  std::optional<QByteArray> iv = std::nullopt);

    /// `decrypt_p2p_aes_body`.  Throws ErrorCode::Protocol on malformed framing
    /// or padding (the reference notes padding validity is not authenticity).
    static QByteArray decryptBody(const QByteArray &body, const QString &password);

    /// `encode_p2p_boolean_body`: one flag byte in front of the payload.
    static QByteArray encodeFlaggedBody(const QByteArray &payload, bool unicodeBytes);

    /// `encode_p2p_frame`: flagged body, optional encryption, Int32LE length.
    static QByteArray encodeFrame(const QByteArray &payload, bool unicodeBytes,
                                  const std::optional<QString> &encryptionPassword,
                                  std::optional<QByteArray> iv = std::nullopt);

    /// `decode_p2p_frame_body`: (payload, flag) from a complete body.
    struct DecodedBody {
        QByteArray payload;
        bool unicodeBytes = false;
    };
    static DecodedBody decodeBody(const QByteArray &body,
                                  const std::optional<QString> &encryptionPassword);

    /// `p2p_disabled_by_tick_entry`: signed 32-bit GetTickCount difference, which
    /// is explicitly NOT wrap-safe elapsed time.  0 <= delta <= 12'000'000.
    static bool disabledByTickEntry(qint32 nowSigned, qint32 savedSigned);
};

} // namespace ultraview::protocol
