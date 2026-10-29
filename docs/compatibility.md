# Compatibility / release gate

Local passing tests establish correspondence with a specific recovered Python
implementation plus synthetic TLS peers. Version 0.2.6 also completed one real
Windows-partner session in the local lab: registration, route, verified relay
TLS, login and a 2240×1400 desktop stream were observed. That is **not** vendor
support or a general compatibility claim: real mouse/keyboard control, longer
sessions, other partner versions and host (sender) mode still need manual
validation. See `verification.md`.

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
- Host (sender) role: a separate 28-field registration identity, relay
  negotiation, inbound 23-field viewer login with proof and throttle, 45-second
  local approval, RGB555/zstd streaming, control/view-only switching and mapped
  pointer/keyboard injection. The wire format, state machine, layout and input
  mapping are covered by offline tests; capture uses the Wayland desktop portal.

## Explicitly unsupported or not session-integrated

- **BitBlt (kind-1 field 5):** fatal UnsupportedOperation, never silently ignored.
- Audio/microphone kinds 42/43/50/51: notices only, no playback, capture, silence
  generation or forwarding. Unknown kinds: unsupported notices, not invented replies.
- P2P handshake/discovery/NAT traversal/transport selection. AES wire helpers are
  fixture-tested but not a working P2P feature. Nonempty request term 16 is refused.
- **Host mode is Wayland-portal-only and not live-validated.** The Host page
  implements registration, inbound relay/login, local approval, RGB555/zstd
  streaming, pointer/key injection and control/view-only switching. It requires a
  Wayland session with xdg-desktop-portal, a portal backend and PipeWire; there is
  no X11 capture fallback, no unattended access, no host chat/clipboard/file
  transfer/audio and no multi-monitor selection beyond the portal's shared
  monitors. The protocol and state machine are covered by offline tests; a real
  Windows viewer controlling this host has not been verified.
- Controller-side clipboard, file transfer, chat and remote cursor shapes remain
  unsupported, as do multi-monitor selection, other pixel codecs/colour depths
  and arbitrary Windows secure-desktop/privileged key sequences.
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
6. Validate host mode on a real Wayland desktop with a real Windows UltraViewer
   viewer: portal permission, assigned ID/password, approval/refusal, wrong-password
   lockout, capture/pacing, pointer and keyboard mapping, control/view-only
   switching and stop/cleanup. The offline suite covers the protocol/state machine
   but not portal or viewer behaviour.
7. Verify clean-host `.deb`/AppImage installation and Wayland/X11 behavior on each
   supported distribution. Verify keyring behavior with a real Secret Service.

Upstream `ACCEPTANCE.md` describes the upstream author's experiments; it is **not
evidence for this C++ binary**. No upstream claim is inherited as native validation.
