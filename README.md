# Ultraview — native Linux controller

[![Native build and offline tests](https://github.com/quytttb/ultraview-linux-client/actions/workflows/ci.yml/badge.svg)](https://github.com/quytttb/ultraview-linux-client/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Experimental **C++20 / Qt 6 Quick** reimplementation of the recovered
UltraViewer controller protocol. It targets Windows UltraViewer partners, not
VNC, RDP or RustDesk. No Wine, browser engine, Python backend or Qt Widgets.

**Status:** native build, byte-level reference fixtures, offline TLS integration
tests and QML rendering smoke test are implemented. The live service answers this
client's registration and partner-request exchange (0.2.2 dry run: the reference's
status-11 refusal for an unknown partner, see [verification](docs/verification.md)).
**A real Windows-partner session has not been validated with this client. This is
not a complete or production-certified UltraViewer implementation.** See
[compatibility](docs/compatibility.md).

## Build on Ubuntu

Requirements: CMake ≥3.24, C++20 compiler, Qt ≥6.5 (Core, Gui, Network, Qml,
Quick, QuickControls2, Test), OpenSSL ≥3 and libzstd. Ubuntu 26.04's distribution
Qt meets the requirement. Ubuntu 24.04's stock Qt 6.4 does **not**: install a
separate Qt ≥6.5 SDK and set `CMAKE_PREFIX_PATH` to its `gcc_64` directory. Do not
mix SDK headers, plugins or libraries with distribution Qt.

On this laptop, privileged commands use `pkexec`; on other Ubuntu systems use
your normal privilege mechanism:

```sh
pkexec apt install build-essential cmake ninja-build pkg-config libssl-dev libzstd-dev \
  qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools qt6-tools-dev-tools \
  qt6-qpa-plugins qt6-image-formats-plugins qt6-wayland \
  qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtquick-controls \
  qml6-module-qtquick-templates qml6-module-qtquick-layouts qml6-module-qtquick-dialogs \
  qml6-module-qtqml qml6-module-qtqml-models qml6-module-qtqml-workerscript \
  qml6-module-qt-labs-folderlistmodel
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
./build/ultraview
```

The compiler's libstdc++ must satisfy your Qt binary's ABI. On the development
laptop, default GCC 11 cannot link system Qt 6.10.2. This build was configured
with `-DCMAKE_CXX_COMPILER=g++-15`; choose that on a **fresh** build directory if
needed. Typical modern Ubuntu compilers are sufficient for their distribution Qt.

Install `libsecret-1-dev` before configuring for platform Secret Service password
storage — the packaged builds and CI do, and they pass
`-DULTRAVIEW_REQUIRE_SECRET_SERVICE=ON` so a packaging build fails instead of
silently compiling the feature out. Without libsecret, remembering passwords is
disabled; there is no plaintext persistence fallback. A working, unlocked keyring
service is still needed at runtime.

Optional: install `qt6-base-private-dev` (Qt ≥ 6.9) before configuring to publish
the application menu to the desktop — KDE Plasma's **Global Menu** panel widget
and appmenu buttons in window decorations then show Session/View/Settings/Help for
this window. Without those headers the client builds and runs unchanged and simply
has no global menu; see [interface](docs/ui.md). The resulting package depends on
the distribution's `qt6-base-private-abi`, like Plasma's own packages.

## Configure and connect

1. Open **Settings → Profiles**. Supply your own registration JSON: `terms`
   (26 strings) and explicit `ansi_codec` (`cp1252` normally). The exact schema is
   described in [protocol](docs/protocol.md). A scaffold can be created from the UI,
   but it is **not** a valid installation identity: machine-specific values must
   come from your authorized, working reference profile. Automatic Linux
   fingerprint generation is unresolved; no identity is invented here.
2. Select your legally obtained **`8bit.dat`** dictionary. It is not included.
   The synthetic test dictionary is not a vendor-dictionary replacement.
3. Optionally select a partner JSON with `request_terms` (17 strings),
   `password_type` and an empty `password`; prefer entering the password in the
   application. The client fills the ID and authentication fields. Leave request
   term 16 empty: P2P is not supported. Without this file the reference-derived
   relay template is used.
4. Leave **Security → Verify chain and host name** selected. Registration verifies
   its connect hostname; relay verifies its route DNS name (or explicit connect-host
   override). The reference's literal `ultraviewer` does not match current service
   certificates and is not the native default. Verification failure is a real failure, not an
   invitation to silently bypass TLS. Host-mismatch-only and accept-any modes
   require an explicit settings selection and show persistent warnings. Accept-any
   removes server authentication and exposes credentials, input and screen data
   to interception. Do not use it on untrusted networks.
5. Enter the authorized partner's ID/password and click **Connect**. Input only
   transmits while connected and enabled. Click the viewer for keyboard focus.
   Disconnect or focus loss releases held keys/buttons on a best-effort basis.

Profiles may contain sensitive identity/password material. Keep them private
(`chmod 600`), never commit them, and never share raw protocol payloads as logs.
Settings and recent IDs use the standard Qt user configuration location
(`~/.config/ultraview/` on this environment); the application does not copy your
profile or dictionary into a package. Endpoint overrides are visible in Settings
and exist for controlled testing/private deployments, not as discovered new servers.

## Offline verification

```sh
QT_QPA_PLATFORM=offscreen ./build/ultraview --self-test --mock-frames 40
ctest --test-dir build --output-on-failure
```

Tests use only loopback TLS peers, synthetic identities and generated fixtures.
Python is only needed to **regenerate comparison fixtures**, not to build, test
the C++ suite, package, or run the application. See [testing](docs/testing.md) for
sanitizers, upstream results, and outstanding manual validation.

## Packages

```sh
# .deb: build against the target distribution's Qt and run tests first
cpack --config build/CPackConfig.cmake -G DEB -B build/packages
# AppImage: provide verified deployment tools and the matching Qt qmake
QMAKE=/usr/bin/qmake6 bash packaging/build-appimage.sh build
```

See [packaging](docs/packaging.md) for prerequisites, limits and smoke checks.
The current `.deb` path is `build/packages/ultraview_0.2.3_amd64.deb`;
its extracted and installed binary passed the QML self-test on the build host.
The local `.deb` can target this distribution only; building here does not make
it compatible with older Ubuntu glibc/Qt. The AppImage recipe is supplied, but an
AppImage has not been built/validated on this machine (deployment tools absent).
The GitHub Actions workflow runs the two-job matrix (sanitizers on/off) on
ubuntu-24.04 with an explicit Qt 6.8.3 SDK and passed both jobs on 2026-10-19
([first green run](https://github.com/quytttb/ultraview-linux-client/actions)).

## Downloads

- [Releases](https://github.com/quytttb/ultraview-linux-client/releases): tagged
  `.deb` packages for Ubuntu 26.04 amd64 with `SHA256SUMS`, built from the tag
  against the distribution's Qt 6 after the CTest suite passed. From v0.2.3 the
  package also includes platform Secret Service password storage (v0.2.2 was
  built without it).
- `dev-build.yml`: every push to `main` that can change the binary uploads a
  `.deb` plus the test log as workflow artifacts (7 days) — the same
  distribution-Qt build, gated the same way.
- `ci.yml` keeps the compatibility matrix green (Qt 6.8 SDK, global menu compiled
  out); `build-release.yml` builds and publishes the release for a `v*.*.*` tag.

## Layout and evidence

- `src/`: C++ protocol, asynchronous transports/session worker, decoder,
  Scene Graph renderer, input and models/security.
- `qml/`: UI components, pages, dialogs, theme and self-drawn SVG icons — nothing
  else (see [interface](docs/ui.md)).
- `tests/`: byte parity, renderer and offline two-channel TLS integration tests.
- `reference/`: upstream comparison material — **not included in this
  repository** (fetch `caohoangphucs/ultraviewer-linux` at the commit recorded in
  [NOTICE.md](NOTICE.md); the local copy is ignored by git); never installed.
- `tools/generate_fixtures.py`: development-only reference runner.
- `tools/make_experimental_profile.py`: development-only registration profile
  drafter; its output is an unproven experiment and it is never installed.
- [Architecture](docs/architecture.md), [protocol](docs/protocol.md),
  [porting plan](docs/porting-plan.md), [interface](docs/ui.md),
  [traceability matrix](docs/traceability.md).
- [Local verification record](docs/verification.md) lists actual checks and blockers.

## Credits

- **Author:** Hai Quy ([@quytttb](https://github.com/quytttb)) — the native client,
  the interface and this documentation. The project's own code is MIT licensed
  ([LICENSE](LICENSE)).
- **Protocol study / reference implementation:** the Python
  [caohoangphucs/ultraviewer-linux](https://github.com/caohoangphucs/ultraviewer-linux)
  project (commit `a0e090fe4de7ae31924a9a19e685c6f74bad74d3`) — the comparison
  baseline used while recovering the wire format and the schema donor for the
  experimental profile tool. Credit for that work goes to its author. The snapshot
  is **not included here** (it has no LICENSE file of its own and is not covered by
  this project's MIT license); see [NOTICE.md](NOTICE.md).
- **Built with:** Qt 6 (LGPL/GPL), OpenSSL, libzstd and, when present, libsecret —
  each under its own license.
- UltraViewer is a trademark of its respective owner; this project is not
  affiliated with or endorsed by that owner.

Read [NOTICE.md](NOTICE.md) before redistribution. No proprietary dictionary,
real partner secrets, or upstream runtime is installed by this project.
