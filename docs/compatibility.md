# Compatibility / release gate

**This native client has not been tested against a real Windows UltraViewer
partner.** Local passing tests establish correspondence with a specific recovered
Python implementation plus synthetic TLS peers. As of 0.2.2 the registration and
partner-request phases *have* been answered by the live service (a dry run with
the local experimental profile and an unknown partner ID returned the reference's
status-11 refusal, see `verification.md`); that is not vendor support, and real
partner authentication, relay/login behaviour and reliable remote control remain
unproven.

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

## Explicitly unsupported or not session-integrated

- **BitBlt (kind-1 field 5):** fatal UnsupportedOperation, never silently ignored.
- Audio/microphone kinds 42/43/50/51: notices only, no playback, capture, silence
  generation or forwarding. Unknown kinds: unsupported notices, not invented replies.
- P2P handshake/discovery/NAT traversal/transport selection. AES wire helpers are
  fixture-tested but not a working P2P feature. Nonempty request term 16 is refused.
- Clipboard, file transfer, chat, remote cursor shapes, remote host/server mode,
  unattended access, multi-monitor selection, other pixel codecs/colour depths,
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
  to registration; a relay authenticates its own DNS identity.
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
6. Verify clean-host `.deb`/AppImage installation and Wayland/X11 behavior on each
   supported distribution. Verify keyring behavior with a real Secret Service.

Upstream `ACCEPTANCE.md` describes the upstream author's experiments; it is **not
evidence for this C++ binary**. No upstream claim is inherited as native validation.
