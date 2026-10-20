# Packaging

## What gets installed

The allowlisted CMake install contains `bin/ultraview`, desktop/icon files,
`README.md`, `NOTICE.md` and these documents. The app's own QML is embedded in
its resources. Qt runtime QML modules/plugins remain runtime dependencies unless
explicitly bundled by a deployment tool.

There is **no install rule** for `reference/`, `tools/`, `tests/`, the test TLS
key, synthetic test dictionary or any user profile. Proprietary `8bit.dat` is
absent from this project. Do not replace this allowlist with a recursive copy of
the workspace. Do not publish the reference-containing source tree without
resolving upstream's missing license.

## Debian package

Build with the **distribution-packaged Qt** on the target distribution. CPack
uses dpkg-shlibdeps for ELF library dependencies and explicit QML/QPA dependencies
for dynamically loaded modules. `dpkg-dev` must be installed. Private Qt SDKs
cannot reliably map to distribution package dependencies; use AppImage instead.

The optional desktop application menu (see [interface](ui.md)) additionally needs
the distribution's private QtGui headers at configure time, which pull in the
build-only package `qt6-base-private-dev` (Qt ≥ 6.9). Where it is available, the
generated binary has one extra runtime requirement —
`qt6-base-private-abi (= <Qt version>)`, provided by the distribution's Qt
libraries themselves, exactly as Plasma's own packages declare it. Where the
headers are missing, CMake reports `KDE global menu support: disabled`, the build
succeeds unchanged and no such dependency is recorded.

```sh
ctest --test-dir build --output-on-failure
cpack --config build/CPackConfig.cmake -G DEB -B build/packages
dpkg-deb --info build/packages/ultraview_0.2.4_amd64.deb
dpkg-deb --contents build/packages/ultraview_0.2.4_amd64.deb
# No root needed to inspect and smoke-test an extracted package:
dpkg-deb --extract build/packages/ultraview_0.2.4_amd64.deb build/package-check
QT_QPA_PLATFORM=offscreen build/package-check/usr/bin/ultraview --self-test
```

To install after review on a compatible Ubuntu machine:
`pkexec apt install ./build/packages/ultraview_0.2.4_amd64.deb`.
This documentation is not authorization for the agent to install it system-wide.
Architecture/file names vary. A binary built against glibc/Qt from Ubuntu 26.04
must not be advertised as an Ubuntu 22.04/24.04 package.

Locally verified: the 0.1.0 package was generated, inspected and extracted; the
0.2.1 package (desktop application menu) was inspected, extracted and installed;
the 0.2.2 package (same sources plus the live connect probe) was generated,
extracted and installed as well; the 0.2.3 package (platform Secret Service
password storage linked in) was generated, extracted and installed too; the
0.2.4 package (relay certificate identity fix) was generated, extracted and
installed as well; the extracted and the installed binaries both passed
`--self-test` on the build host.
This is not a clean-host installation test or a signed release.

## CI packages (dev build / release)

Two workflows package the same way this document does — distribution Qt on
`ubuntu-26.04`, CMake/Ninja RelWithDebInfo, `ctest` as the gate, then
`cpack` and an extracted-package `--self-test`:

- `.github/workflows/dev-build.yml` — pushes to `main` that touch `src/`, `qml/`,
  `tests/`, `packaging/`, `CMakeLists.txt` or the workflows upload
  `ultraview-dev-deb-<sha>` (7 days) plus `ultraview-dev-test-log-<sha>`; commits
  whose message contains `chore(release):` are left to the release workflow.
- `.github/workflows/build-release.yml` — a `vMAJOR.MINOR.PATCH` tag (or a manual
  run naming an existing tag) checks that the tag equals the `project(... VERSION)`
  in `CMakeLists.txt`, then publishes a GitHub Release with the `.deb` and
  `SHA256SUMS`.

Because both build against Ubuntu 26.04's Qt 6, the packages depend on that
distribution's Qt libraries (`libqt6core6t64 (>= 6.10.2)`,
`qt6-base-private-abi (= 6.10.2)`, `qt6-declarative-private-abi (= 6.10.2)`) and
are not advertised for older Ubuntu releases. Building without
`qt6-base-private-dev` compiles the global menu out; every workflow here installs
it where the distribution offers it. Both also install `libsecret-1-dev` and pass
`-DULTRAVIEW_REQUIRE_SECRET_SERVICE=ON`, so a packaging build fails rather than
silently dropping platform Secret Service password storage; the package then
depends on `libsecret-1-0`.

Releases attach `ultraview_<version>_amd64.deb` and `SHA256SUMS`; from
[v0.2.3](https://github.com/quytttb/ultraview-linux-client/releases/tag/v0.2.3)
the package includes password storage (the earlier
[v0.2.2](https://github.com/quytttb/ultraview-linux-client/releases/tag/v0.2.2)
release was built without it). The first published package was downloaded again
after publication and its checksum, dependency list and extracted `--self-test`
were verified (see [verification](verification.md)). Install from a downloaded
file with `sudo apt install ./ultraview_*.deb` — the version in `dpkg-query` then
matches the release tag.

## AppImage recipe

Install/pin/check signatures or hashes for `linuxdeploy`, `linuxdeploy-plugin-qt`
and its AppImage output helper separately. They must be executable on PATH.
The script does not download executables. Use qmake from the exact Qt used to
build the binary, with that Qt's tools/plugins available in PATH.

```sh
QMAKE=/usr/bin/qmake6 bash packaging/build-appimage.sh build
QT_QPA_PLATFORM=offscreen build/Ultraview-x86_64.AppImage \
  --appimage-extract-and-run --self-test --mock-frames 40
```

The recipe creates a **fresh** staging directory inside the build directory,
installs the allowlist, invokes the Qt deployment plugin with the QML source path,
and retains the AppDir for inspection. It requests image-format and TLS plugins.
Check the resulting AppDir contains Qt Quick/Controls/Dialogs dependencies, JPEG,
OpenSSL TLS backend/library and the correct QPA platform plugins, and run on a
clean host without developer Qt paths. Host CAs are still needed; do not bundle
or enable the test CA. Build on the oldest supported glibc host for portability;
AppImage does not make glibc backward-compatible.

**AppImage generation is not locally verified:** deployment tools are absent on
the development machine. The recipe fails early with installation instructions.
Do not claim a portable artifact just because the native binary builds.
Distribution must also satisfy bundled libraries' licensing obligations.

## CI

`.github/workflows/ci.yml` configures Debug builds with ASan/UBSan on/off, installs
an explicit Qt 6.8 SDK, runs offline CTest, and uploads logs plus a native staging
artifact. That artifact is **not** a self-contained package: it needs matching Qt.
The SDK's Qt is older than 6.9, so CI builds the client on the graceful path
without the optional desktop application menu (`KDE global menu support:
disabled`); the menu tests and the sources compile the same way in both
configurations. GitHub execution, AppImage output, package installation and
release signing are not claimed as completed by the presence of a workflow file.
