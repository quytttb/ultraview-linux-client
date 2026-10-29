# Recovered protocol and native policy

Evidence is the pinned Python snapshot and generated fixtures, **not** a complete
vendor specification. Indices below are zero based. LE means little endian.
Native defensive bounds are not claims about the Windows implementation.

## Transport

- Default registration endpoint: `functions3.ultraviewer.net:443`. The reference's
  TLS name literal is `ultraviewer`; native 0.1.1 instead verifies the actual
  registration hostname, matching the service's `*.ultraviewer.net` certificate.
  The relay channel authenticates that same service certificate name: the
  reference wraps both sockets with one `TLS_TARGET_HOST`, and the route's
  `tcp_server_hostname` is only the HTTP `Host:` header of the relay preface,
  never a certificate name. An explicit relay certificate name overrides the
  registration name; a relay connect-host override authenticates that host.
  The production relay presents only its leaf certificate; the client adds the
  issuers of the already verified registration chain as extra trust anchors on
  top of the system store (never replacing it) so the relay chain verifies
  without weakening the policy.
  The HTTP Host header is separate from TLS identity. Relay socket port is
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

The sender (host) role has its own registration: a 28-field command `3` carrying
a separate stable identity, 8-digit login password, display name and MAC. It uses
the same encryption and framing, and its `3:` reply has 11 or more fields. The
sender role, its relay/login flow and its screen/input messages are documented in
[host (sender) mode](host-mode.md); the controller flow above is unchanged.

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
- Kind 3: remote cursor selection (see
  [Remote cursor](#remote-cursor-commands-3-4-and-28)). A
  four-byte field 1 is signed Int32 LE; a shape that is not cached is requested
  with ASCII `|||4` followed immediately by its decimal representation, no extra
  delimiter. Without that field, report unsupported; do not invent a reply.
- Kind 4: remote cursor shape transfer, a four-field payload
  `[4][hash Int32][declared length Int32][zstd(.cur)]`.
- Kind 5: legacy text clipboard as a field array; field 1 is the UTF-16LE text.
- Kind 12: chat message; the text is field 1 (UTF-16LE).
- Kinds 13/14/15/16/21: file transfer offer/accept/chunk/cancel/ack.
- Kinds 27/41: the partner closed / terminated the session; the session stops.
- Kind 28: pointer position `[28][x Int16][y Int16]`.
- Kinds 29/30: the partner started / stopped typing.
- Kind 33: modern compressed clipboard (three fields, see below).
- Kind 42: audio silence; field 1 is a little-endian byte count.
- Kind 43: G.722 computer-sound payload in field 1.
- Kind 48: the host changed its tile size to 8, 16 or 32 pixels.
- Kind 49: view-only state; field 1 is one byte, 0 = full control, 1 = view only.
- Kinds 50/51: partner microphone audio, explicitly refused with a notice.
  Unknown kinds are reported as unsupported. Malformed framing and BitBlt are
  fatal.

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

## Peer application messages

The commands below travel inside relay frames exactly like screen packets. Their
relay flag is part of each command definition — text commands use UTF-16LE
flag 1, numeric/byte commands ASCII flag 0 — so `ControlCommands` returns the
payload and flag together. Code lives in `src/protocol/ControlCommands.cpp`.
The semantics follow the GPL `remmina-plugin-ultraviewer` protocol reference; the
native state machines and tests are separate implementations (see
[NOTICE.md](../NOTICE.md)).

### Text clipboard (commands 5 and 33)

- **Modern upload (33):** ASCII header `33|||<UTF-16LE byte count>|||` followed by
  a dictionary-free Zstandard frame of the UTF-16LE text, relay flag 0.
- **Modern download (33):** a three-field packet `[33][uint32 LE uncompressed
  byte count][zstd frame]`. The declared count is checked against the decoder
  budget before decompression.
- **Legacy (5):** UTF-16LE `5|||<text>`, relay flag 1. It is used for the empty
  string, which the host rejects in the modern form, and accepted on input both
  as a delimited text payload and as a field array.
- Only text is synchronized; images, rich text and the X11 primary selection are
  out of scope because the protocol carries UTF-16 text only.
- `|||` inside the text is harmless in the modern form (it sits inside a
  length-prefixed frame); the delimited legacy form refuses it instead of sending
  a message the host would split.
- Local echo is suppressed for 1000 ms after a remote update, and the last remote
  text is remembered, because X11/Wayland report a programmatic clipboard write
  as a normal change. `ClipboardBridge` owns this guard in the GUI thread.
- Budget: 1 MiB per message in either direction.

### Remote cursor (commands 3, 4 and 28)

- `28|||1` / `28|||0` (ASCII, flag 0) enables/disables pointer tracking. It is
  sent after the login acknowledgement only when the preference is enabled; a
  preference set before connecting is recorded and sent during the handshake.
- Command 3 carries a signed Int32 shape hash. The all-zero/hidden selection hash
  `0xb84d1a70` clears the overlay. A cached shape is applied immediately, a miss
  is requested once and remembered, so a burst of selections cannot become a
  request storm.
- The request bytes are the reference's `|||4<hash>` form
  (`InputCodec::sizeFollowUp`), the spelling the live service has been observed
  answering. `ControlCommands::cursorShapeRequest` builds the documentation's
  `4|||<hash>` alternative and is kept for tests, not used by the session.
- Command 4 carries `[4][hash Int32][declared .cur length Int32][zstd]`. The
  declared length must be positive and at most 1 MiB; the decompressed resource
  is decoded completely or rejected, never partially applied.
- `.cur` decoding (`decoder/CursorDecoder`): `ICONDIR` (type must be 2) +
  `ICONDIRENTRY` (hotspot, byte count, offset) + DIB. `biHeight` is twice the
  cursor height because the XOR and AND bitmaps are stacked; the DIB is read
  bottom-up. 1, 4, 8, 16, 24 and 32 bpp are supported, up to 256×256, and the
  hotspot is clamped into the image. Mask rule: AND 0 = opaque XOR pixel,
  AND 1 + XOR black = transparent, AND 1 + XOR white = inverted, drawn as opaque
  black (the frame cannot invert what is behind it).
- Command 28 carries the partner pointer as two signed Int16 values. The overlay
  is drawn by `RemoteScreenItem` at the remote coordinates, scaled with the
  desktop, and hidden when no shape is known.
- `decoder/CursorCache` is a bounded LRU (32 shapes) shared with the pending
  request set; it is cleared on reconnect.

### File transfer (commands 13, 14, 15, 16 and 21)

| Command | Meaning | Delimited form (viewer → host) | Multipart form (host → viewer) |
|---|---|---|---|
| 13 | offer | `13|||id|||name|||size` | `[13][id][name][size double]` |
| 14 | accept | `14|||id|||1` | `[14][id][int32]` |
| 15 | chunk | `15|||id|||offset|||bytes` | `[15][id][offset double][bytes]` |
| 16 | cancel | `16|||id` | `[16][id]` |
| 21 | ack | `21|||id` | `[21][id]` |

- Numeric fields in the multipart direction are IEEE-754 doubles in little-endian
  byte order (not integers); the delimited direction uses ASCII decimals.
- One chunk is acknowledged before the next is sent: a 32,000-byte window. The
  file-size ceiling is `999999999999999` bytes, at most 8 transfers exist at once,
  and an inbound chunk is capped at 65,536 bytes for a host that rounded up.
- Transfer IDs are generated locally and echoed by the host.
- Incoming offers are only written after an explicit accept (the default);
  `transferAutoAccept` skips the question. Received files go to
  `<Downloads>/Ultraview` under a sanitized, non-overwriting name. A name with a
  directory separator, `..`, a control character or `|||` is refused.
- A chunk whose offset does not continue the stream, an oversized file or a
  failed write cancels the transfer and removes the partial file. Both sides are
  always answered with an ack or a cancel, so no partner waits for a window that
  will not advance.
- The host may send the delimited ASCII form instead of a field array; the
  session falls back to it rather than failing the session.

### Computer sound (commands 42, 43 and 46)

- `46|||-1` starts the host's WASAPI loopback stream, `46|||0` stops it (UTF-16LE,
  flag 1). The connect path sends the start command explicitly when the
  preference is already enabled, because `AudioPlayer::setEnabled` is idempotent.
- Command 43 field 1 is a G.722 64 kbit/s stream; one byte decodes to two 16-bit
  samples at 16 kHz. The decoder keeps its state across packets, so a dropped or
  reordered packet is audible but never desynchronizes the stream permanently.
- Command 42 asks for a number of silent PCM bytes; the count is bounded to 1 MiB
  before allocation.
- Output is native PipeWire (`ULTRAVIEW_HAVE_PIPEWIRE_AUDIO`) when the
  development headers are present: the graph converts 16 kHz mono S16LE to the
  device rate, so the client never resamples by hand. Without PipeWire a null
  sink keeps the protocol path working and the UI says that no output is
  available. A stalled device drops audio instead of stalling the session worker.
- The G.722 decoder is a C++ port of the GPL plugin's decoder and is golden-vector
  tested against it (see [NOTICE.md](../NOTICE.md)).

### Chat and typing (commands 12, 29 and 30)

- Outbound chat is UTF-16LE `12|||<text>` (flag 1). Text longer than 32,768
  UTF-16 bytes or containing `|||` is refused before sending, because the host
  would split or truncate it.
- Inbound chat is a multipart command 12 whose field 1 is the text.
- `29` / `30` (bare codes, and also accepted as one-byte fields) mean the partner
  started / stopped typing. The local typing indicator is sent when the input text
  changes and cleared after 1.8 s of inactivity.

### Session actions and display (commands 22, 23, 27, 31, 32, 40, 41, 48, 49 and 53)

- `22` Ctrl+Alt+Del, `40` reboot and `53` lock are bare UTF-16LE action codes
  (flag 1). They are single commands with no reply; the host applies them on its
  own secure desktop.
- `23|||-1` shows the partner wallpaper, `23|||0` blanks it (UTF-16LE).
- `31|||<30|45|70>` selects speed / balanced / quality (ASCII, flag 0). Other
  values are refused by the command builder.
- `32|||2` forces the RGB555 colour mode. The builder exists and is tested, but
  this client already requests RGB555 in the login viewport hint and does not send
  it during a session.
- Kind 49 reports the partner's view-only state; while it is set the client warns
  that input is ignored there and can disable local input.
- Kind 48 accepts a tile-size change to 8, 16 or 32; other values are reported
  and the current size is kept.
- Kinds 27/41 mean the partner closed / terminated the session; the session stops
  with a notice rather than waiting for a timeout.

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
