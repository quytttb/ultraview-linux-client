# Recovered protocol and native policy

Evidence is the pinned Python snapshot and generated fixtures, **not** a complete
vendor specification. Indices below are zero based. LE means little endian.
Native defensive bounds are not claims about the Windows implementation.

## Transport

- Default registration endpoint: `functions3.ultraviewer.net:443`. The reference's
  TLS name literal is `ultraviewer`; native 0.1.1 instead verifies the actual
  registration hostname, matching the service's `*.ultraviewer.net` certificate.
  Relay connects to the route address and verifies its DNS hostname, unless an
  explicit relay connect-host override is configured (then verify that host).
  Registration-only name overrides never affect relay verification. The HTTP
  Host header is separate from TLS identity. Relay socket port is
  443 unless explicitly overridden. Route TCP port is the **target** port in the
  relay envelope, not the relay server's socket port.
- HTTP tunnel request is `POST /UVService/ HTTP/1.1` with the literal
  `Content-length: 8589934590`, reference user-agent/content-type/connection
  fields, `Host:` and CRLFCRLF. `http_preface` fixtures pin all bytes and order.
  Registration requires status line exactly `HTTP/1.1 200 OK`; relay accepts
  `HTTP/1.1 200 ` prefix. Header budget: 16384 bytes.
- Frames are `<length UInt32 LE><body>`. The reference encoder writes signed
  Int32 lengths; supported positive lengths have identical representation.
  `FrameStream` preserves the unusual `buffer.size() > 4` guard: a standalone
  zero-body frame stays pending until another byte arrives. Split and multiple
  frames are supported. Native default body limit is 1 MiB, configurable up to
  64 MiB. Invalid oversized buffered data is cleared before refusal.
- A direct inbound body starts `01`, followed by payload. A relay body starts
  `00 <flag> <IPv4 UInt32 LE> <port UInt16 LE> <payload>`. Address value is built
  with the first dotted octet most significant, then written LE: `1.2.3.4`
  becomes address bytes `04 03 02 01`. Nonzero flag indicates UTF-16LE bytes.

## Registration / identity

`RegistrationCodec::buildRegistrationCommand()` colon-joins 26 terms in the
explicit ANSI codec. Term 0 is `3`, terms 3/5 are `0`, term 12 is empty, and term
22 contains three colon-separated decimal tick strings. Therefore a raw split of
the command has more than 26 fields: do not count the flattened fields as terms.
NUL is forbidden; only term 22 may contain colons. Other machine-specific terms
are not generated or assigned fictional Linux equivalents.

Outbound body is `01 00 || TripleDES-CBC-PKCS7(ANSI(command))`. The recovered
obfuscation password consists of character values 5, 10, 15, 20, 25. Key is
SHA1(UTF16LE(password)), zero-extended/truncated to 24 bytes; IV is
SHA1(UTF16LE(empty)), truncated to 8 bytes. Empty plaintext encrypts to a full
padding block. These are extracted compatibility constants, not private partner
credentials. Responses are direct `01 || plaintext` bodies, not assumed to be
encrypted. Identity response subset: `3:<source port>:<controller ID>:<IP>`;
extra fields are tolerated, numeric ranges checked.

Fingerprint digest is uppercase MD5 of the provided preimage. The fallback
helper concatenates MAC plus supplied Windows creation-tick values with no
separator. It is fixture-tested as a helper, **not wired into automatic Linux
identity generation**. Referral/version/OS-combine helpers likewise preserve
observed arithmetic without creating a new Windows environment on Linux.

## Request, route and login

Command 8 has 17 colon-joined ANSI terms. Template comes from the reference.
Positions 1/2/6/7 carry partner ID, password value, issued controller ID, and
lowercase MD5(ANSI(controllerID + registrationTerm2)). For password type 0 the
request password value is lowercase MD5(ANSI(32 spaces + password)); other types
use the provided password unchanged. P2P suffix must be empty in this relay-only
session implementation.

Status-1 command 8 replies carry LAN/public endpoints, computer-name metadata,
TCP relay host, target TCP port, custom-password indicator/session ID and an
optional partner secret at field 20. `RouteParser` uses the reference positions
without interpreting unresolved fields. Status 2: wrong password; 11: offline or
unknown ID; 99: cannot connect (network error); other statuses: explicit unknown
refusal. At most four registration replies are considered for the route.

Relay login is `login:` plus **22** colon-joined fields, encoded UTF-16LE without
BOM/terminator, relay flag 1. Positions 0/1/12 are authentication fields. Type 0
uses a fresh eight-ASCII-digit salt at 0, lowercase MD5(ANSI(salt + password)) at 1,
and the request-password digest at 12. Other types use 32 spaces at 0 and the
password at 1/12. Other positions are copied from `ControlSession.connect()`:
viewport hint at 2/3; issued ID 9; local hostname 10; 16-digit marker 14; partner
secret 19; version `6.6.133` at 20. Unresolved fixed values remain reference
values, not explanations invented by the port. Delimiter/NUL values are refused.

The reference's strict ANSI subset is preserved (`cp1252`, `latin1`, `ascii`,
`utf-8`). Windows replacement fallback/DBCS encoding is not emulated. Codecs never
silently replace unrepresentable text in authentication inputs.

## Session packets and screen

Payload fields are concatenated `<UInt32 LE byte count><bytes>` values. Field 0
must contain a discriminator byte:

- Kind 0: fields 1/2 are Int32 LE width/height, checked before canvas allocation.
  Send ASCII `17`, publish the initial black canvas and enter Connected.
- Kind 1: exactly 11 fields. Apply in this order: scroll copy (6), cache
  references (9), JPEG atlas (7/8), RGB555 rectangles (1/2), cache additions (10),
  LRU eviction. Send ASCII `18` after a successfully decoded/retained update.
- Kind 3: a four-byte field 1 is signed Int32 LE; respond with ASCII
  `|||4` followed immediately by its decimal representation, no extra delimiter.
  Without that field, report unsupported; do not invent a reply.
- Kinds 42/43/50/51: audio, explicitly refused with notices. Unknown kinds are
  reported as unsupported. Malformed framing and BitBlt are fatal.

Field 1 contains UInt16 LE x/y/width/height records; field 2 is one zstd frame
with exactly `sum(width*height*2)` decompressed bytes, using the supplied dictionary.
Pixels are packed LE RGB555. The reference Pillow `BGR;15`, orientation `1`
interprets rows **top-down**; the C++ decoder follows it. It does not reverse rows
based on an unverified Windows-DIB assumption. Five-bit components use reference
scaling (see fixtures/tests). A single missing field 1 or 2 skips this path as in
the reference; nonempty field 5 (BitBlt) is explicitly unsupported.

JPEG bytes at 7 decode to RGB and flip vertically as `ImageOps.flip` does.
Field 8 is Int16 LE x/y/w/h records. `atlas_layout` visits block columns then
rows, wrapping atlas y to the next block-width column. Default block size is 32;
the fixture with a 32×79 atlas detects an accidental 16-pixel implementation.

Field 6 is `<Int16 delta,x,y,w,h>` plus `<Int16 rx,ry,rw,rh>` subcopies. It reads
the pre-update canvas into a shifted intermediate: destination `(x,y-delta)`.
Only the listed contained subrectangles are restored. Field 9 consists of
`<Int16 x,y><Int32 key>` records. References move keys to the LRU end. Field 10 is
`<Int16 x,y,w,h><Int32 key>`; only absent keys are captured from the final canvas.
Missing references fail atomically. Default eviction limit is 1000 entries.

## Input and helper-only P2P

All input is ASCII inside relay flag 0:

- Move: `8|||512|||0|||x|||y` (signed Int16 coordinates).
- Buttons: `8|||<513/514/516/517/519/520>` for left/right/middle down/up.
- Wheel: `8|||522|||<signed delta shifted left 16>`.
- Keyboard: `8|||<256/257>|||<VK>`; system down/up use 260/261. VK is 0..255.

`P2pCodec` implements reference AES-256-CBC-PKCS7 framing only for tests/future
work: IV16 precedes ciphertext, plaintext is `<Boolean flag><payload>`. ASCII
password MD5 contributes digest[0:15] + digest + zero byte as a 32-byte key.
Decoded flag `>0` is true (not only 1). This is **not** a P2P handshake, traversal,
discovery or enabled session transport. The session refuses a nonempty P2P suffix.
