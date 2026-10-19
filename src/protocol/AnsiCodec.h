#pragma once
//
// Strict single/double byte text codec for the ANSI strings on the wire.
//
// The recovered protocol joins fields with ':' and encodes them with the Windows
// ANSI code page of the *sending* machine.  The reference implementation takes
// that code page from the user profile (`ansi_codec`, `cp1252` in the recovered
// profile) and decodes/encodes with `errors="strict"`, i.e. it refuses to
// silently replace characters.  The native client keeps that strictness but does
// not depend on Qt5Compat/QTextCodec:
//
//   * cp1252 / latin1 / ascii are implemented with an explicit table and are
//     byte-exact in both directions (strict: unmappable input is an error),
//   * utf-8 is supported with a round-trip check so that lossy conversions are
//     rejected instead of being silently accepted.
//
#include <QByteArray>
#include <QString>
#include <QStringView>

#include <optional>

namespace ultraview::protocol {

class AnsiCodec {
public:
    enum class Kind { Cp1252, Latin1, Ascii, Utf8 };

    /// Returns nullopt for names the client cannot honour strictly.  Accepts the
    /// spellings seen in reference profiles and in Python/iconv naming.
    static std::optional<AnsiCodec> fromName(QStringView name);
    static AnsiCodec cp1252() { return AnsiCodec{Kind::Cp1252}; }
    static AnsiCodec utf8() { return AnsiCodec{Kind::Utf8}; }

    Kind kind() const noexcept { return m_kind; }
    QString name() const;

    /// Encode to bytes.  Throws ErrorCode::Protocol when a character is not
    /// representable (never silently substitutes '?' like Windows would).
    QByteArray encode(const QString &text) const;

    /// Decode bytes.  Throws ErrorCode::Protocol on undefined byte sequences.
    QString decode(const QByteArray &bytes) const;

    /// Decode with `1:1` semantics used for numeric/enum fields, refusing the
    /// same inputs as the reference (`bytes.decode(codec, errors="strict")`).
    QString decodeField(const QByteArray &bytes) const { return decode(bytes); }

    /// True when the codec can represent every character of `text`.
    bool canEncode(const QString &text) const;

private:
    explicit AnsiCodec(Kind kind) : m_kind(kind) {}
    Kind m_kind;
};

} // namespace ultraview::protocol
