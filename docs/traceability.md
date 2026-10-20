# Traceability matrix (source → native code → checks → status)

Snapshot: upstream commit `a0e090fe4de7ae31924a9a19e685c6f74bad74d3`.
Paths in the source column below are relative to `reference/` (the upstream
comparison snapshot, not part of this repository — see
[NOTICE.md](../NOTICE.md) for its origin and commit); native paths are
relative to the project root. **Parity** means synthetic/reference verification,
not real Windows-partner validation. **Reference-only** means retained/audited,
not a feature shipped in the native application. There are no unlisted promises
of complete function-by-function recovery.

## Core modules

### `uv_reference.py`

- Framing, typed/relay bodies, address order, HTTP preface, byte-array fields →
  `src/protocol/PacketCodec.{h,cpp}` → `tst_packet_codec` with `transport_frames`,
  `typed_transport_frames`, `relay_frames`, `transport_bodies`, `frame_stream`,
  `http_preface`, `byte_array_fields` fixtures → **parity**; native limits added.
- Key derivation/registration encryption, command-3 schema/identity, fingerprint,
  fallback fingerprint, Referral.ini Int32, OS-version combine and version compare
  → `RegistrationCodec`, `Digest`, `AnsiCodec` → `tst_registration`, corresponding
  `vfunction_key`, `registration_cipher`, `command3_payloads`, `fingerprint`,
  `fallback_fingerprint`, `referral_int32`, `os_version_combine`, `version_compare`
  fixtures → **parity helpers**. No OS identity/registry acquisition is ported;
  profile *files* are validated by `models/ProfileLoader` (`tst_profiles`), and
  the development-only `tools/make_experimental_profile.py` can draft one
  experimentally — the application itself never generates identity values.
- Request-password value, salt digest, login fields/command, P2P suffix and
  SendData string encodings → `AuthenticationCodec`, `AnsiCodec`, UTF-16LE
  digest helper → `tst_auth`, `tst_session` → **parity for supported encodings**.
  UTF-8/base64 display-name metadata helpers (`encode_utf8_base64`,
  `encode_base64_if_not_ascii`, `login_display_name`) are **not independently
  exposed native APIs**; the session uses the explicit hostname schema from
  `control_session.py`, not the wider unresolved name-selection policy.
- P2P Boolean/AES key/encrypt/decrypt/framing/tick helpers → `P2pCodec` →
  `tst_auth`, `p2p` fixtures → **helper parity, not session-integrated**.
  The tick helper uses widened signed subtraction, matching the reference rather
  than incorrectly wrapping a negative difference into a short elapsed interval.
- CachedItem/import/export container → `decoder/TileCache` →
  `tst_screen_decode::cacheContainerFormat/cacheRoundTripThroughContainer` →
  **container parity only**. Native tile payload is local RGB888, not recovered
  vendor serialization. Invalid/nonpositive dimensions are a native refusal.
- `atlas_layout` → `JpegAtlasDecoder::layout` → `atlas_layout` fixtures and
  `tst_screen_decode` → **parity**.
- `dib_stride`, `_rows`, `extract_tile`, `pack_atlas`, `restore_atlas`,
  `tile_sample_score` → **reference-only** utilities. `rows` fixture captures
  extract/pack/restore examples for audit, but the C++ suite does **not** claim
  native parity for these unported helpers. Qt QImage row copies and atlas layout
  provide the receiver's equivalent operations; spatial scoring is sender-side
  analysis and unused. No invented native implementation stands in for them.

### `registration_transport.py`

TLS+HTTP, exact registration status line, framed `01 00` encrypted send and direct
mode-1 responses → `network/RegistrationTransport`, `HttpTunnelSocket`,
`protocol/RegistrationCodec`, `security/TlsPolicy` → `tst_registration`,
`tst_packet_codec`, two-channel `tst_session` → **offline integration verified**.
TLS defaults intentionally differ: strict verification, explicit insecure option.

### `session_transport.py`

Relay TLS+HTTP 200 prefix, framing/body decoding and addressed send →
`network/RelayTransport`, `HttpTunnelSocket`, `PacketCodec` → `tst_packet_codec`,
`tst_session` → **offline integration verified**. Endpoint overrides are explicit
test/deployment policy. The relay reuses the registration certificate name (the
reference wraps both channels with one TLS name); the route hostname is only the
HTTP `Host:` header of the relay preface. P2P is not substituted for this relay
path.

### `control_session.py`

Registration→request→route→login schema, deadlines, screen dispatcher, `17`/`18`,
kind-3 follow-up and session teardown → `core/SessionManager`,
`network/ConnectionManager`, `AuthenticationCodec`, `ScreenDecoder` →
`tst_session` → **offline integration verified**, not live acceptance. Native
worker/credit coalescing replaces Python queues; audio and unknown operations
produce notices, BitBlt/malformed image operations fail explicitly.

### `target_route.py`

Status-1 route indices, short optional secret, status errors →
`protocol/RouteParser`/`TargetRoute` → `tst_route`, `route`, `route_short`,
`route_status` fixtures and `tst_session::refusalStatusCodes` → **parity**.

### `screen_decode.py`

ZstdDictionaryDecoder, dimensions, RGB555 rectangles, scroll-before-cache,
cache/JPEG/pixel/addition ordering, LRU and rejection atomicity →
`decoder/{ZstdDecoder,FrameBuffer,ScreenDecoder,JpegAtlasDecoder,TileCache}` →
`tst_screen_decode`, eight `screen_sequences`, `desktop_dims`, `atlas_layout`
fixtures → **supported-path parity**. Component ramps and actual Pillow behavior
pin top-down RGB555 with floor component scaling; JPEG explicitly flips vertically.
Native budgets add stricter refusal. Proprietary dictionary not redistributed.

### `receive_pipeline.py`

Byte-array packet/discriminator routing → `PacketCodec::decodeByteArrayFields`,
`ConnectionManager::handleSessionPacket` → `tst_packet_codec`, `tst_session` →
**screen/control subset ported**. Audio sink consumption, microphone forwarding,
silence and failure-forward-as-null semantics are **not implemented**. The native
session refuses audio kinds explicitly; upstream audio tests remain reference-only.

### `input_control.py`

Mouse move/buttons/wheel/VK/system-key strings and release-on-failure →
`protocol/InputCodec`, `input/{InputController,KeyboardMapper}` →
`tst_keyboard_mapper`, `input_commands` fixtures, `tst_session::inputCommandsReachThePartner`
→ **wire parity**. Qt→VK/layout/focus handling is new native glue, tested separately;
real Windows keyboard-layout/shortcut effects remain unverified.

## Every upstream test file

- `test_reference.py` (14 cases): framing/address/preface/cache/layout/identity
  → `tst_packet_codec`, `tst_registration`, `tst_screen_decode` fixtures. DIB row
  utility and spatial-score cases are **reference-only**, not ported test claims.
- `test_byte_array_fields.py` (2): explicit wire/null-collapse/truncation →
  `PacketCodec`, `tst_packet_codec::byteArrayFields/byteArrayFieldCountValidation`
  and `tst_session` malformed-packet paths → **ported subset**; not every original
  truncation prefix is claimed as a distinct C++ case.
- `test_login_password.py` (3): type-0 fixed digests, nonzero raw-password path,
  salt validation → `AuthenticationCodec`, `tst_auth` login/request checks → **parity**.
- `test_p2p_wire.py` (2): AES framing/fragmentation and noncanonical receive flag
  → `P2pCodec`, `tst_auth::p2p*`, `tst_packet_codec::frameStream` → **helper parity**,
  no native P2P connection implied.
- `test_input_control.py` (3): Notepad command strings, signed wheel, release
  safety → `InputCodec`/`InputController`, `tst_keyboard_mapper`, `tst_session`
  → **wire parity + native release test**, not observed Notepad effects.
- `test_screen_decode.py` (8): top-down rectangles, later-rectangle atomicity,
  cache reuse/missing-reference failure, JPEG-before-pixel, scroll/original pixels,
  32-pixel atlas and login sizes → native decoder tests/fixtures → **supported
  behaviors covered**; synthetic cache/scroll fixtures seed coloured pixels before
  reuse so a no-op cannot pass simply by preserving a black canvas.
- `test_receive_pipeline.py` (3): field preservation → PacketCodec/session tests;
  audio/microphone/silence/sink-failure cases → **reference-only / native unsupported**.
- `test_version.py` (3): component precedence, fourth-component ignored and
  supported domain → `RegistrationCodec::versionEqualOrGreater`, `tst_registration`
  fixture checks → **parity helper**, not an update-service implementation.
- `test_viewer.py` (2): HTTP cross-origin/token/input validation → **not applicable
  to native UI**; no localhost web API is shipped. Native input/session gating
  and QML pixel smoke test replace UI concerns. Upstream HTML-token regex case
  errors before reaching its intended assertion; unmodified baseline is 39/40.

## Remaining upstream artifacts

- `viewer.py`, `server.py`: Python HTTP/web transport/API shell → **not shipped**;
  replaced by SessionManager/ConnectionViewModel and Qt Network/QML, no local web server.
- `viewer.html`: browser UI/canvas/input → **not shipped**; replaced by
  `qml/pages/{ConnectPage,ViewerPage}`, QML components and `RemoteScreenItem`.
- `app.py`, `app_window.py`: GTK/WebKit launcher → **not shipped**; replaced by
  `src/app/main.cpp`, QGuiApplication, QQmlApplicationEngine, `qml/Main.qml`.
- `register_once.py`, `request_partner_once.py`: manual Python probes →
  **reference-only**; native app runs the full asynchronous control flow instead
  of launching them. Fixture generator is a development tool, never a backend.
  The experimental profile drafter (`tools/make_experimental_profile.py`) is also
  development-only and not installed; the live service accepts its registration
  identity up to the partner-request phase (status-11 dry run in
  `verification.md`), while a relay/login exchange with a real partner is unproven.
- `run.sh`, `requirements.txt`: Python runtime setup → **reference-only**; replaced
  by root CMake/README, optional fixture-generation prerequisites only.
- `README.md`: upstream usage/feature claims → audited; new root `README.md`
  documents native build/limits. Upstream UI stack and claimed live support are
  not inherited as properties of this client.
- `RUN.md`: upstream operating notes → comparison only; native usage/security
  policy is in root README and compatibility/protocol/testing docs.
- `ACCEPTANCE.md`: upstream live experiment claims → **unverified for this port**;
  native manual release gate is explicitly still open.
- `MANIFEST.json`: original snapshot hash inventory → retained without pretending
  excluded `8bit.dat` is present. It does not cover all newer upstream wrapper
  files, and six retained files already differ from its recorded hashes in the
  pinned upstream clone (`viewer.py`, `test_screen_decode.py`, `RUN.md`,
  `control_session.py`, `viewer.html`, `screen_decode.py`). This is an upstream
  stale-manifest limitation, not a local edit. All 30 retained files were compared
  byte-for-byte with the pinned Git clone and match. `NOTICE.md` records the Git
  commit. Native install is an allowlist,
  not a reproduction of that manifest. No proprietary asset is included.
- `8bit.dat`: **excluded**; user supplied only. `tests/data/test-dict.bin` is a
  separate synthetic dictionary with provenance/hash in fixture metadata.

## Native-only checks

`tst_viewport`, `tst_render_item`, app `--self-test` (which also checks that every
embedded theme icon resource is present), the additional input-release and
backpressure tests validate Qt-specific behavior that has no direct Python
counterpart. The live connect probe (`--connect-attempt` with
`app_connect_probe_unconfigured`) is a native check too: it drives one real
attempt through the client's own path and reads the service's answer without an
interface, which the Python reference only does interactively. The QML
theme/components (`qml/`, see [interface](ui.md)) likewise
have no Python counterpart, and neither does the desktop application menu
(`src/platform/GlobalMenu.cpp` with `tst_globalmenu`): upstream is a browser page
with its own HTML chrome, this client's menu is desktop integration the reference
never had. CMake/CPack and the CI recipe replace upstream packaging. These are not
substitutes for a real partner, production keyring, clean-host package or
hardware-backend validation.
