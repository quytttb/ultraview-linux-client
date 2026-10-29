# Host (sender) mode

Host mode lets a Windows UltraViewer control this Linux computer. This client
registers its own ID, accepts an inbound viewer through the UltraViewer relay,
asks the local user for approval, streams the desktop as RGB555/zstd rectangles
and injects the viewer's pointer and keyboard events through the Wayland desktop
portal.

It is implemented and covered by offline protocol/state-machine tests, but it has
**not** been validated against a real Windows viewer or a live desktop-portal
prompt. It is not an unattended-access feature and makes no vendor-certification
claim. The byte representation follows the pinned sender reference described in
[protocol](protocol.md); parsing is stricter than the reference.

## Scope and security boundaries

- Hosting starts only from the explicit **Start hosting** action on the Host
  page. Opening the page loads the identity; it never registers or captures.
- Every incoming viewer needs a local **Allow control**, **View only** or
  **Refuse** decision. A request expires after 45 seconds; there is no
  auto-approve and no unattended mode.
- Registration and relay connections use the normal verified TLS policy. Weak
  modes remain an explicit, warned Settings choice; host mode never enables them
  itself.
- The password, login proof and profile are never written to the status line,
  diagnostics or application log.
- Host mode carries screen and input only. Chat (12/29/30), clipboard (5/33),
  file transfer (13–16/21) and computer sound (42/43/46) are not implemented on
  the host side; unsupported viewer messages are ignored rather than guessed.
- Only Wayland capture exists. There is no X11 fallback; without a Wayland
  session with `xdg-desktop-portal`, a portal backend and PipeWire, the Host page
  reports that screen sharing is unavailable.

## Identity and profile

The host identity is separate from the controller's 26-term registration
profile. A controller registration must not take over the ID assigned to this
host.

| Item | Value |
|---|---|
| Path | `<AppConfig>/host-profile.json` (`~/.config/ultraview/host-profile.json` here) |
| Directory | `0700` |
| File | `0600` |
| Stored fields | `device_identity`, `password`, `display_name` |
| Device identity | 32 uppercase hex characters, stable per installation |
| Password | 8 decimal digits, shown locally and entered by the viewer |

`HostProfile::loadOrCreate()` creates the device identity from the system random
source and the password from eight random decimal digits. The password is the
viewer login password; the optional master-password proof stays empty. An
existing profile is never replaced. Invalid JSON, a malformed identity or a
malformed password is reported with the file path so the user can fix or delete
it deliberately.

Machine metadata is refreshed on every load but not stored: the default route's
IPv4 address, its interface MAC, the kernel release and the current user name.
When no usable unicast MAC is available, a stable locally administered address is
derived from the device identity. Non-ASCII user names are sent as base64 UTF-8
so the registration stays printable ASCII.

The profile contains a secret. It is excluded by `.gitignore`, never installed by
the package, and must never be attached to an issue.

## Registration

Registration uses command `3`: **28 colon-joined printable-ASCII fields**,
encrypted with the same recovered TripleDES wrapper and framed like the
controller's registration command. The reply arrives as a plaintext `3:` payload
on the command channel.

| Field | Content |
|---:|---|
| 0 | `3` |
| 1 | 32-hex device identity |
| 2 | 8-digit host password |
| 3 | `0` |
| 4 | primary local IPv4 address |
| 5 | `0` |
| 6 | base64 display name (the machine host name by default) |
| 7 | host version, `6.6.133` |
| 8 | 12-hex hardware MAC |
| 9 | empty |
| 10 | operating system `Build\|Major\|0\|Minor\|0\|0` from the kernel release |
| 11 | `0` |
| 12 | empty |
| 13 | master-password proof (empty in this client) |
| 14 | `0` |
| 15 | empty |
| 16 | user name (ASCII, or base64 UTF-8) |
| 17–24 | eight `0` fields |
| 25–27 | empty |

The builder rejects a term containing a non-printable character, a colon or a
non-ASCII character, so the display name and user name are encoded before they
are joined.

A valid reply has 11–128 fields:

| Field | Content |
|---:|---|
| 0 | `3` |
| 1 | public/relay port, 1–65535 |
| 2 | assigned numeric ID |
| 3 | public IPv4 address |
| 6 | semicolon-separated default relay IPs |
| 7 | status (signed 32-bit; retained for diagnostics) |
| 9 | semicolon-separated relay host names |
| 10 | opaque server token |

Fields 4/5/8 and any extra fields are tolerated. The host waits up to 20 seconds
for the reply. A syntactically valid reply is accepted; field 7 is not currently
used to reject a registration. After registration the command channel stays open
for relay requests and receives a `2` keepalive every 60 seconds; the relay socket
gets a `ping` system payload on the same interval. If the command channel closes,
host mode stops and reports the failure.

## Relay negotiation

The command channel delivers an incoming request as `11:` or `17:`:

- `11:<relay-ip>:<unused>:<relay-hostname>` — at least 4 fields; the first
  address/hostname pair from fields 1 and 3 is used.
- `17:<tag>:<hash>:<addresses>:<hostnames>:<tri-state>:<int16>:<int32>:<tri-state>`
  — at least 9 fields. Fields 3 and 4 are semicolon-separated lists; when empty
  the host uses fields 6 and 9 of the registration reply. The first pair that
  forms a valid endpoint is used.

Every address must be a canonical dotted IPv4 literal. Every host name must be
`ultraviewer.net` or a subdomain, is lowercased and is used only for the HTTP
`Host:` header. One request may contain at most 64 addresses and host names.

The relay socket is opened to the requested IPv4 address on port 443 (or the
configured testing override). TLS authenticates the registration service name by
default. On top of the system trust store the host adds the issuers of the
already verified registration chain (`chain.mid(1)`) as extra trust anchors, so
the relay's leaf-only presentation verifies without weakening policy. An explicit
relay certificate-name override is respected.

After the HTTP tunnel is ready the host sends the system payload `0` and waits up
to 15 seconds for the relay's `0:<port>:<ip>` reply. It then sends the encrypted
command-channel advertisement

```text
17:<tag>:<hash>:<request-address>:<relay-port>:<relay-ip>:0:|0
```

A duplicate request for the same endpoint while the relay is open is answered
again once the relay address is known. A different request while a viewer is
connected is ignored with a status message. Relay failure or closure ends the
viewer and returns to the waiting state.

## Viewer login and approval

Relay payloads may be direct or wrapped in the relay envelope. A UTF-16LE flag is
converted to UTF-8 and control characters other than tab/LF/CR are rejected.
A login is `login:` plus **23 colon-separated ASCII fields** (22 colons).

Positions the host reads:

| Field | Content | Host check |
|---:|---|---|
| 0 | `login` | exact |
| 1 | nonce | 8 decimal digits |
| 2 | password proof | lowercase MD5(nonce + password), constant-time compare |
| 3, 4 | viewer viewport width/height | positive integers |
| 10 | claimed viewer ID | non-zero decimal |
| 11 | display name | truncated to 50 characters for the prompt |
| 12 | control mode | signed 16-bit |
| 13 | optional custom proof | lowercase MD5(32 spaces + password), only when a custom proof is configured (this client sends none) |
| 15 | connection hash key | non-empty, at most 128 characters |
| 16 | display colors | must be `2` (16-bit mode) |
| 17 | display quality | signed 32-bit |
| 21 | viewer version | `major.minor.patch`, major ≥ 6 |

The other positions must be present but are not interpreted. A login with the
wrong proof receives command `22` on the viewer's route. Parsing failures also
count as failures and receive `22`. Five failures in a row lock new attempts for
five minutes; attempts during the lockout are refused with `22` and do not
extend it, and a successful login resets the counter. A correct proof with an
unsupported colour mode, an older viewer or a missing connection hash receives
command `27`.

Once verified, the viewer enters the approval phase and the local UI shows its
ID and name. **Allow control** initializes the session with input enabled;
**View only** initializes it with input disabled; **Refuse** (and Escape in the
dialog) sends `27`. If the local user does not answer within 45 seconds, the
session ends and the viewer receives `27`.

A repeated login from the same route and with the same connection hash while the
viewer is already streaming re-sends initialization for the last frame and resets
the frame sequence. A second route is refused with `27`.

## Screen stream

The host sends command `0` initialization as an 11-field multipart message:

| Field | Content |
|---:|---|
| 0 | byte `0` |
| 1, 2 | little-endian Int32 canvas width and height |
| 3, 4 | empty |
| 5 | byte `0` |
| 6 | version text `6.6.133` |
| 7 | byte `1` |
| 8 | monitor rectangles `left:top:right:bottom|...`, or `0:0:width:height|` |
| 9, 10 | little-endian Int32 `0` |

Immediately after initialization the host sends command `49` with one byte:
`0` allows keyboard/mouse control, `1` means view-only. The same command is sent
when the local user changes **Allow keyboard and mouse** during a session.

The viewer acknowledges initialization with `17`. The host waits up to 20 seconds
for that acknowledgement and then sends the first screen rectangle.

Each frame is command `1` with 11 multipart fields; field 0 is byte `1`, field 1
is the rectangle `left:top:right:bottom` (all `0,0,width,height`), field 2 is the
zstd-compressed pixel rectangle, and field 4 is the little-endian Int32 sequence
number. Compression level is 1.

Pixels are packed little-endian RGB555: blue in bits 0–4, green 5–9, red 10–14.
`HostFrame` stores rows bottom-up, as Windows DIBs do; the encoder reverses them
so the compressed rectangle is top-down, matching the controller's reference
decode. The first frame number is `-2147483646` and increases by one per frame.

Flow control is one outstanding frame. The viewer acknowledges each frame with
`18`; the next capture is then due after 40 ms. If the pixels and layout are
unchanged, the host skips encoding and checks again after 250 ms. A missing
acknowledgement for 20 seconds ends the viewer. The capture budget is 1920×1080
for one display and 3840×2160 (union) for multiple displays; larger desktops are
box-filtered down without ever being upscaled, to at most 8192 per dimension.
A monitor/layout change re-sends initialization without resetting the frame
number.

A viewer asking for an unsupported colour mode with `32|||1` is disconnected.
An ASCII `20` keepalive is answered with command `20`; `19` disconnects the
viewer. All frame payloads are bounded by the configured maximum frame size.

## Input

Input is accepted only while the viewer is streaming and control is allowed.

- Mouse messages use `8|||<message>|||...` with Windows messages `0x200`–`0x20a`.
  Movement carries `x` and `y`; button and wheel messages carry the packed
  client coordinates and the wheel delta. The host maps the viewer canvas to the
  shared source rectangle, sends absolute pointer motion to the portal stream
  under the pointer, and translates buttons to portal button codes. Wheel steps
  are accumulated with a remainder so fractional deltas are not lost.
- Keyboard messages arrive either as `8|||256|||65|||-1|||-1` /
  `8|||257|||65|||-1|||-1` (key down/up, plus the system-key variants) or as
  the 11-byte binary form: byte `0x1e`, three `|` characters, kind 1–4, virtual
  key, little-endian set-1 scan code, little-endian UTF-16 unit and a trailing
  byte ≤ 1. Malformed or out-of-range payloads are dropped.
- Keys and buttons are mapped with the recovered US-layout table to portal
  keysyms/keycodes. Unmapped keys are not invented. Held keys and buttons are
  released when control is disabled, the viewer disconnects, or hosting stops.
- If the portal grants only one device class, the other class is ignored rather
  than sent through an unavailable API.

## Capture backend

`WaylandPortalBackend` uses `org.freedesktop.portal.RemoteDesktop` and
`org.freedesktop.portal.ScreenCast` over QtDBus:

1. `CreateSession` on `RemoteDesktop`.
2. `SelectDevices` with keyboard and pointer.
3. `SelectSources` on `ScreenCast` for the same session handle, with monitor
   sources, multiple monitors and embedded cursor. Remote-desktop sessions cannot
   persist, so no `persist_mode`/`restore_token` is requested; KDE rejects one
   with "Remote desktop sessions cannot persist".
4. `Start` on `RemoteDesktop`, which may show the compositor's remote-control
   permission dialog (up to 180 seconds), and returns the shared devices and the
   PipeWire stream nodes. `OpenPipeWireRemote` only finds those streams on the
   remote-desktop session, so `Start` must be sent there rather than to
   `ScreenCast`.
5. `OpenPipeWireRemote` on `ScreenCast` and one PipeWire stream per shared
   monitor.

Captures are composed from the latest frame of each selected monitor into the
union rectangle; pointer coordinates select the monitor under the cursor. The
backend sends pointer motion/button/axis and keyboard keysym/keycode notifications
through the portal session. Portal setup calls time out after 30 seconds.

The portal reports each stream's size in logical (compositor) pixels, while
PipeWire delivers frames at the physical buffer size; for example, a 1920×1080
frame can belong to a 1536×864 logical stream at scale 1.25. The capture canvas
and viewer layout use the physical frame pixels, and pointer coordinates are
converted back to the logical stream space before the notification is sent.
Every `RemoteDesktop.Notify*` method takes `(session, options, ...)`; the options
dictionary must be the second argument.

The runtime dependencies are `xdg-desktop-portal`, a portal backend such as
`xdg-desktop-portal-kde` (GNOME and wlroots backends are alternatives) and a
PipeWire server. The `.deb` recommends them. Builds that must ship host support
configure `-DULTRAVIEW_REQUIRE_WAYLAND_HOST=ON` so missing QtDBus or
`libpipewire-0.3` headers fail instead of silently removing the feature.

## Not implemented

- X11 capture/injection and non-portal Wayland capture.
- Unattended access, saved viewer approvals, or approval without a local user.
- Host-side chat, clipboard, file transfer and audio.
- Multiple simultaneous viewers (one viewer; a request on another route is
  refused).
- A monitor picker: the portal's own source selection is respected and the
  selected monitors are composed into one canvas.
- Privileged/secure-desktop Windows key sequences beyond the ordinary keys the
  viewer sends; no elevation helper is installed.

## Verification status

`tst_host` covers the 28-field registration, reply parsing, relay request and
availability, 23-field login proofs, throttle, approval timeout, frame
encoding/pacing, layout scaling, and Windows-key/pointer mapping. The full suite
is offline. The remaining validation is end to end: a real Windows UltraViewer
viewer, the portal permission prompt on a live KDE Wayland session, PipeWire
frame delivery, pointer/keyboard injection, control/view-only switching, and
shutdown while the portal prompt is unanswered. See
[verification](verification.md) for the dated record.
