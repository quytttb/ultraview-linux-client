#pragma once
//
// Small shared helpers for the *textual* part of the wire format.
//
// The recovered protocol encodes several fields as Windows BSTR bytes, i.e.
// UTF-16LE without BOM and without terminator, and derives digests over those
// bytes (see `uv_reference.derive_vfunction_key_bytes`,
// `login_numeric_salt_digest` and `registration_fingerprint_digest`).  Python
// uses `encode("utf-16le", errors="surrogatepass")`, which emits lone surrogates
// verbatim; Qt's `toUtf8`/`fromUtf8` cannot express that, so the conversion is
// written out explicitly here and shared by the codecs that need it.
//
#include <QByteArray>
#include <QString>

namespace ultraview::protocol {

/// UTF-16LE code units of `text`, including lone surrogates (Python
/// `surrogatepass` semantics).
QByteArray utf16leBytes(const QString &text);

/// UTF-16LE code units of the first `maxCodeUnits` units (the recovered login
/// helper slices the encoded bytes, which can leave a dangling surrogate).
QByteArray utf16leBytesLimited(const QString &text, int maxCodeUnits);

/// MD5 of `data` as lowercase hexadecimal (32 characters).
QString md5HexLower(const QByteArray &data);

/// MD5 of `data` as uppercase hexadecimal (`VFunction.GetMD5` + VB6 `UCase`).
QString md5HexUpper(const QByteArray &data);

/// SHA-1 of `data`.
QByteArray sha1Bytes(const QByteArray &data);

} // namespace ultraview::protocol
