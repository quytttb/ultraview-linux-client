# Compatibility / release gate

Local passing tests establish correspondence with a specific recovered Python
implementation plus synthetic TLS peers. The pinned Python snapshot covers the
screen/input/login path; the peer application features added in 0.3.0 were
implemented against the GPL-2.0-or-later `remmina-plugin-ultraviewer` protocol
reference and are covered by offline codec and state-machine tests, not by a live
partner. Version 0.2.6 also completed one real Windows-partner session in the
local lab: registration, route, verified relay TLS, login and a 2240×1400 desktop
stream were observed. That is **not** vendor support or a general compatibility
claim: real mouse/keyboard control, longer sessions, other partner versions and
the peer features against a live partner still need manual validation. See
`verification.md`.

## Implemented and locally tested

- Reference byte representation for HTTP preface, LE framing, relay addressing,
  registration 3DES/key derivation, command 3 identity, command 8 request/route,
  password types, salts/digests and UTF-16LE login.
- Worker-thread session lifecycle, timeouts, wrong-password/offline/cannot-connect
  refusals, channel loss, teardown, explicit certificate policy and loopback input.
- RGB555/zstd rectangles, JPEG atlas layout/vertical flip, cache references and
  additions/LRU, scroll-copy reference ordering, atomic malformed-update rejection.
  Full 5-bit component ramps pin `floor(v*255/31)` (bit replication is different).
- Scene Graph QQuickItem/QSGSimpleTextureNode rendering, mirror patch ownership,
  bounded coalescing/credit flow, aspect transforms and offscreen pixel smoke test.
- Native input encoding and a supported Qt→VK subset, including original-mapping
  key releases. Settings, recent IDs and scaffold/profile loading are implemented.
- Peer application messages (0.3.0), each with its own bounded codec and tests:
  text clipboard in both encodings (5/33) with a 1 s echo guard, remote hardware
  cursor (3/4/28) with a bounded LRU shape cache, file transfer (13–16/21) with a
  one-chunk window and explicit accept, G.722 computer sound (42/43/46) through a
  PipeWire sink, chat and typing indicators (12/29/30), the session actions
  (22/23/31/40/53) and the partner's view-only state (49). The UI is the chat
  side panel, the transfer side panel and the session-tools popup.

## Explicitly unsupported or not session-integrated

- **BitBlt (kind-1 field 5):** fatal UnsupportedOperation, never silently ignored.
- Microphone kinds 50/51 (partner recording): refused with a notice. The viewer
  never captures or forwards a microphone.
- P2P handshake/discovery/NAT traversal/transport selection. AES wire helpers are
  fixture-tested but not a working P2P feature. Nonempty request term 16 is refused.
- **Host mode is Wayland-portal-only.** The Host page implements registration,
  inbound relay/login, local approval, RGB555/zstd streaming, pointer/key
  injection and control/view-only switching. It requires a Wayland session with
  xdg-desktop-portal, a portal backend and PipeWire; there is no X11 capture
  fallback, no unattended access, no host chat/clipboard/file transfer/audio and
  no multi-monitor selection beyond the portal's shared monitors. Portal capture
  and registration are live-checked on KDE Wayland; a real Windows viewer
  controlling this host has not been verified. A Linux host backend
  outside the packaged UI is not implied.
- **Remote controller host/server role** beyond the above: unattended access,
  arbitrary Windows secure-desktop/privileged key sequences beyond the documented
  Ctrl+Alt+Del/lock/reboot commands, and host-side peer tools remain unsupported.
- Computer sound requires the distribution's PipeWire client library; a build
  without it still decodes the stream and reports that no output is available.
  Audio capture, mixing, volume control and device selection are not implemented.
- Clipboard synchronization is text only: no images, rich text, file lists or X11
  primary selection, and no clipboard history.
- Automatic Linux registration fingerprint/installation provisioning inside the
  product. Scaffolds deliberately leave unresolved values blank and cannot connect
  by themselves; the dev-only `tools/make_experimental_profile.py` can draft an
  experimental profile outside the application. Its registration identity is
  accepted by the live service up to the partner-request phase (status-11 dry run,
  0.2.2); a relay/login exchange with a real partner is still unproven. The profile
  is not installed by the package.
- Importing vendor tile-cache pixel encoding. The recovered **container** layout
  is implemented; this client's optional local helper serializes RGB888 payloads.
  No cache import/export UI or network cache-container negotiation is enabled.
  Byte-count mismatch is refused, but matching byte counts alone cannot prove
  an arbitrary foreign payload is the same format; do not import untrusted vendor
  cache files through these helper APIs.
- Native equivalents of reference sender-side spatial sampling and raw DIB
  extract/pack/restore utilities: not needed by this receive-only image path.

## Intentional policy differences

- TLS chain/name verification is **on by default**. The reference accepts any
  certificate. Compatibility modes must be selected explicitly and remain warned.
  A missing CA, expired certificate or wrong name is not silently bypassed.
  Version 0.1.1 fixes the initial native default: authenticate endpoint hostnames,
  not the reference's literal `ultraviewer`. The registration override is scoped
  to registration; the relay authenticates the same recovered service certificate
  name while the route hostname remains the HTTP tunnel host field. If the relay
  omits its intermediate certificate, the client completes that chain from the
  already verified registration channel rather than weakening verification.
- Stronger limits: checked framing, header, canvas, JPEG and decompression budgets;
  a 256 MiB tile-cache budget; bounded channel buffers. Resource exhaustion is
  refused instead of following unchecked allocations. The memory budget can
  reject traffic even when reference entry-count eviction would eventually fit.
- Missing/nonpositive cached-item dimensions are rejected sooner than reference
  `CachedItem.parse`. Unsupported packets are named rather than treated as success.
- Scene Graph uses public APIs and uploads the entire RGB32 mirror on change,
  not partial GPU texture updates. Two in-flight patches and dirty-region
  coalescing limit queues; this is not a measured high-refresh-rate guarantee.
- Peer features default to the cautious choice: a file offer waits for an
  explicit accept (no silent write into the download directory), received names
  are sanitized and never overwrite an existing file, transfers and clipboard
  messages have fixed byte budgets, and the transfer list is capped at 8
  concurrent entries. Audio drops packets when no sink is available instead of
  buffering without bound.
- Random salts/markers use the system RNG; VB Rnd state is not emulated. Strict
  supported ANSI encodings do not reproduce all Windows code-page fallbacks.

RGB555 rows follow the reference's top-down Pillow decoding; JPEG atlas rows
follow its explicit vertical flip. Fixture generation originally assumed both
were bottom-up. That assumption was corrected using executed reference behavior,
not by changing the vendored reference. JPEG test samples now come from an actual
reference decode; they are not hand-assigned colour claims. Tests remain synthetic.

## Outstanding validation (do not mark complete)

1. Obtain server acceptance for a controller profile. The experimental profile's
   registration and partner-request exchange is now answered by the live service
   (0.2.2 dry run, status 11 for an unknown ID); what remains is a relay/login
   exchange with a real partner, and keeping the vendor dictionary legally supplied
   (the local installation of `8bit.dat` satisfies the latter half only).
2. With the Windows owner present, record server/partner version and certificate
   behavior; verify chain/name policy without weakening it automatically.
3. Connect, compare known Windows screen content visually and check the RGB555
   row direction/colour ramp with real traffic. A login acknowledgement alone
   does not prove control works.
4. Verify pointer corners/centre, buttons, wheel ±120, letters, modifiers held in
   different release orders, focus loss, input-disable, disconnect/reconnect.
5. Exercise resolution changes, cache churn and sustained updates for a stated
   duration. Check bounded memory, real render backend, and thread shutdown.
6. Validate the 0.3.0 peer features with a real Windows partner: clipboard in
   both directions, cursor shape and pointer overlay, file offer/accept/chunk
   pacing in both directions, G.722 playback through PipeWire, chat and typing,
   the action commands, view-only state and a tile-size change. Offline tests
   prove the codecs and state machines, not the partner's behaviour.
7. Verify clean-host `.deb`/AppImage installation and Wayland/X11 behavior on each
   supported distribution. Verify keyring behavior with a real Secret Service.
8. Validate host mode with a real Windows UltraViewer viewer: whether it accepts
   the assigned ID/password, approval/refusal, wrong-password lockout over the
   relay, capture/pacing, pointer and keyboard mapping, control/view-only
   switching and stop/cleanup. Portal permission/capture and registration are
   already live-checked; the viewer side is not.

Upstream `ACCEPTANCE.md` describes the upstream author's experiments; it is **not
evidence for this C++ binary**. No upstream claim is inherited as native validation.
