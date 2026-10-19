# Porting plan and completed checkpoints

## 1. Audit / freeze the comparison source — completed

Snapshot: `caohoangphucs/ultraviewer-linux`, commit
`a0e090fe4de7ae31924a9a19e685c6f74bad74d3`. Read transport, control, route, decode,
input and receive-pipeline modules plus every test. Keep the original source in
`reference/` for comparison (locally; the snapshot is not part of the published
repository — see `NOTICE.md`); exclude `8bit.dat` and private JSON profiles. Preserve
the original manifest (including the entry for the deliberately absent dictionary).
No upstream LICENSE was present; see the distribution warning in `NOTICE.md`.

Audit result: independently runnable reference tests give 39/40 passing, with
`test_viewer.py` failing its HTML-token regex before its intended assertion.
That test/reference source is not edited to manufacture a passing baseline.

## 2. Native protocol contract — completed for the supported subset

Implement byte framing, HTTP preface, strict ANSI and UTF-16LE, EVP registration
crypto, identities, command-8 route/status/authentication, login and input codecs.
Run `tools/generate_fixtures.py` against the reference; tests compare external
expected bytes rather than only encode/decode self-roundtrips. Pin noncanonical
receive flags, empty PKCS#7 plaintext and strict-four-byte framing behavior.

P2P is helper-only, not an enabled session feature. Raw DIB/sender sampling and
audio receive semantics are mapped to unsupported/reference-only status rather
than presented as complete ports. See the traceability matrix for every source.

## 3. Asynchronous transport / session — completed offline

Qt Network TLS+HTTP channels replace Python sockets. A worker thread serializes
network events, deadlines and decode operations; the GUI remains event driven.
An offline two-channel TLS peer verifies encrypted registration requests, actual
login fields/digests, route handling, relay input and teardown. It uses a test CA,
not accept-any. Separate tests prove name mismatch fails and explicit accept-any
warns. Registration loss after route resolution is nonfatal; relay loss is fatal.

## 4. Decoder / ownership — completed for supported paths

Native libzstd, Qt JPEG, RGB555 conversion, scroll-copy and tile LRU replace Pillow
and ctypes. Validate entire plans before mutation. Bound arithmetic/allocations.
Reference-generated pixels and hashes verify output, including every 5-bit
component, row orientation, missing cache references, and the 32-pixel atlas case.
Immutable dirty-rectangle patches transfer ownership; credit return flushes final
coalesced state even when no more network frames arrive.

## 5. Qt Quick Scene Graph — completed locally

`RemoteScreenItem` is a custom QQuickItem/QSGSimpleTextureNode. Decode is off the
GUI thread, no QQuickPaintedItem or framebuffer-object shortcut. Texture creation
is render-thread-only. Public Qt texture upload re-uploads the full mirror:
partial GPU upload is **not** claimed. View transforms/input mapping and a real
QML viewer pixel grab are exercised by native tests and `--self-test`.

## 6. Product shell / distribution — implemented, validation bounded

QML connection/viewer/settings/about UI, profile scaffolds, diagnostics, recent
IDs and optional libsecret storage. CMake install allowlist, desktop/icon,
CPack `.deb`, AppImage recipe and CI workflow supplied. Runtime packages never
include the reference Python tree, development generator, tests, private profiles
or vendor dictionary. See testing/packaging docs for what was actually run.

## 7. Real interoperability acceptance — not completed

Requires an authorized Windows partner and a real installation profile/dictionary.
Run the checklist in `compatibility.md`, collect redacted evidence (version,
duration, screen comparisons, input effects), and only then revise feature claims.
Do not use upstream acceptance statements, mocks or a login ack as a substitute.
AppImage portability, clean-host package installation, hardware accelerated
high-refresh rendering and production keyring flows also remain release gates.
