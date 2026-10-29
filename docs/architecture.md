# Architecture

## Runtime boundaries

Production code is C++20. QML declares UI and forwards events, but does not parse
wire packets, authenticate, decode compressed images or implement networking.
There is no Python process, JavaScript backend, web view, Qt Widgets dependency,
browser canvas or alternate remote desktop protocol in the application.

- `core/SessionManager` is the GUI-thread facade. It owns a `QThread`, registers
  metatypes and forwards requests/credit returns to `network/ConnectionManager`
  with queued invocations. Shutdown closes sockets inside their owning thread.
- `network/ConnectionManager` lives in the session worker thread. Network events,
  authentication and decoding are serialized there, not on the GUI thread. It
  validates requests before creating a connection, handles deadlines and owns
  the two transports and `ScreenDecoder`.
- `HttpTunnelSocket` owns `QSslSocket` and phase timers; `RegistrationTransport`
  and `RelayTransport` perform channel-specific status validation/framing.
  `protocol/` contains pure wire codecs, strict ANSI encoding, digest helpers
  and route parsing. Error types distinguish malformed, unsupported, TLS,
  authentication, resource and network failures.
- `models/ConnectionViewModel` exposes session/settings/input/recent IDs to QML.
  `ProfileLoader` loads user-supplied schema values. `SettingsModel` writes
  ordinary preferences, never partner passwords. `CredentialStore` optionally
  uses libsecret; unavailable builds explicitly refuse password persistence.
- Peer features keep the same split: `protocol/{ClipboardCodec,ControlCommands,
  FileTransferCodec,ZstdCodec}` are pure codecs, `decoder/{CursorDecoder,
  CursorCache}` and `transfer/FileTransferService` run in the worker thread,
  `audio/{G722Decoder,AudioPlayer,PipeWireAudioSink}` decode and play the partner
  sound, and `platform/ClipboardBridge` owns the GUI-thread clipboard and its echo
  guard. `ConnectionViewModel` caches their reported state for QML; the panels and
  the tools popup only send `Q_INVOKABLE` actions.
- `models/HostViewModel` is the GUI-thread facade for host mode. It owns a
  separate `QThread` with `host/HostManager`, loads the private host identity only
  when the Host page is opened or hosting starts, and mirrors state/approval
  signals. `host/HostProfile` writes the stable identity/password to
  `<AppConfig>/host-profile.json` (0700 directory, 0600 file);
  `host/HostProtocol` and `host/HostSession` implement registration, relay
  negotiation, viewer login/throttle, 45-second approval, RGB555/zstd frame
  pacing and input gating. `host/HostBackend`/`host/WaylandPortal` capture the
  desktop through the xdg-desktop-portal ScreenCast + RemoteDesktop APIs over
  QtDBus/PipeWire and inject mapped keys/pointer events. The host page and
  approval dialog are QML only; hosting starts only from the explicit **Start
  hosting** action. The wire format and security rules are in
  [host mode](host-mode.md).
- `platform/GlobalMenu` is the only desktop-integration piece. It describes the
  application menu (Session/View/Settings/Help) and mirrors the session state; the
  platform theme of Qt turns that into the desktop's application menu (KDE's
  global menu) when the desktop offers one, and Qt owns the D-Bus object and the
  window-manager announcement. It contains no protocol logic, draws nothing
  itself, and — without the private Qt QPA headers — exports no menu while QML
  still binds its `statisticsVisible` state.

## Lifecycle

`Disconnected → Connecting → Registering → RequestingPartner → RelayConnecting
→ Authenticating → Connected`. Stop transitions to `Disconnected`; fatal errors
transition to `Failed`. `Disconnecting` is available in the shared state model
but the current stop implementation tears down immediately rather than awaiting
a graceful close. Registration command 3 yields a controller ID; command 8 yields
the target route; relay login acknowledgement yields the actual desktop size.

Registration is drained after route resolution. Its loss then becomes a notice,
not a fatal control-session error. Relay loss remains fatal. Microphone audio and
unknown packet kinds are reported without inventing behavior; the computer-sound
stream is decoded and played. Malformed
screen updates and BitBlt operations end the session rather than leaving the user
viewing a falsely authoritative screen. There is no automatic reconnect loop.

## Pixels and ownership

1. `ScreenDecoder` validates the entire kind-1 packet into a plan before applying
   operations in reference order. Bad records, decompression mismatch and missing
   cache references cannot partially modify the canvas. Allocation failure is not
   a recoverable protocol operation; the implementation does not promise a full
   rollback under arbitrary process-wide out-of-memory failure.
2. The worker exclusively owns `FrameBuffer`'s RGB32 canvas and tile LRU. It
   accumulates a union dirty rectangle. `takeUpdate()` copies that rectangle into
   an immutable `shared_ptr<const FrameUpdate>`; live mutable pixels are never
   shared with the GUI/render thread.
3. At most two patches are in flight. Further updates coalesce on the worker
   canvas. Returned credits flush dirty state even without another network
   packet. Acknowledgement `18` means the update was decoded and retained, not
   that the GPU presented it. Intermediate visual states may be coalesced; final
   pixels are not dropped.
4. `render/RemoteScreenItem` is a custom **QQuickItem** whose `updatePaintNode()`
   uses **QSGSimpleTextureNode**. It is neither QQuickPaintedItem nor
   QQuickFramebufferObject. GUI-thread submissions update its private mirror;
   Qt synchronizes the GUI and render threads during Scene Graph synchronization.
   Textures are created only inside `updatePaintNode()`.
5. Public Qt `createTextureFromImage()` uploads the **whole mirror** once per
   changed render frame. Dirty-region GPU uploads are not implemented: those
   would require a different public texture abstraction or private RHI APIs.
   The renderer bounds pending rectangle bookkeeping at 32 and falls back to a
   full upload. A size change requires a full-frame patch, and every patch must
   fit completely. Rejected nonempty submissions return their credit.

Canvas dimensions are at most 8192 per edge and 32 Mi pixels. Zstd output defaults
to 64 MiB; JPEG dimensions are checked before decoding. Tile entries default to
1000 and also obey a native 256 MiB aggregate budget, rejecting over-budget updates
before canvas mutation. Safety budgets are native policy, not recovered wire
constants. Socket buffers and frame sizes are bounded independently. Total
resident memory includes both canvases, patches, decoded temporary images,
cache and driver textures, so the per-resource limits are not a small global
process-memory guarantee.

## Input and presentation

`ViewportMap` implements fit, crop and stretch with the same transform used by
the texture node. The surrounding viewport clips crop overflow. Pointer events
outside the desktop are rejected. `InputController` creates ASCII commands via
`InputCodec`; `KeyboardMapper` maps Qt keys to Windows VK values. Held keys retain
their original VK/system mapping so modifier changes cannot change key-up into
a different operation. Focus loss, disable and disconnect release held inputs.
Linux/window-manager reserved shortcuts, IME composition and arbitrary Windows
keyboard layouts are not guaranteed.

## Security boundary

TLS policy is centralized and defaults to chain plus expected-name verification.
Accept-any is explicit and warned, never used as error recovery. OpenSSL EVP
contexts/ciphers and zstd contexts use RAII. The protocol's recovered MD5/3DES
operations are compatibility mechanisms, not modern security recommendations.
Raw payloads and partner secrets are not diagnostic content. Qt's implicitly
shared strings mean clearing/filling secrets is best effort, not a secure-memory
erasure guarantee. Profile files are managed by the user; the application cannot
make an externally supplied plaintext profile into secure storage retroactively.
