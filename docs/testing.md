# Testing and evidence

## Build / offline test commands

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++-15
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen ./build/ultraview --self-test --mock-frames 40
```

Use the compiler matching your Qt ABI (GCC 15 is the verified laptop setting, not
a universal requirement). No vendor server or real partner is contacted by CTest.
All integration peers bind loopback. Test certificate/private key are intentionally
public synthetic test credentials, never installed or trusted in production.

## Native suite

- `tst_packet_codec`: fixture-exact transport/typed/relay bytes, HTTP preface,
  address byte order, byte-array field lengths, fragmented/multiple frames,
  pending empty frames and frame limits.
- `tst_registration`: key derivation, encryption/decryption (including empty
  plaintext), command-3 terms/identity, fingerprints and version/referral helpers.
- `tst_auth`: request/login fields and digests, invalid salts/delimiters,
  helper-only P2P AES/flags/key derivation and reference fixtures.
- `tst_route`: full/short routes, refusals and malformed numeric/field schemas.
- `tst_profiles`: JSON registration/partner profile loading against every
  recovered schema constraint (term counts, fixed terms, delimiter/NUL limits,
  tick strings, codec names), the default request template and the template
  writers (round trip plus the explicit overwrite contract). All profiles are
  synthetic files in a temporary directory; no real identity is used or committed.
- `tst_keyboard_mapper`: byte-exact mouse/key/wheel payloads, VK mappings and
  release of the exact original system/OEM mapping.
- `tst_screen_decode`: pixel sequences, all-component ramps, atlas block layout,
  error atomicity, cache containers/LRU and immutable frame handoff.
- `tst_viewport`: aspect transforms, clamped boundaries and TLS-policy defaults,
  selective hostname waiver, explicit accept-any and extra CA behavior.
- `tst_render_item`: mirror bytes/hash, bounds and size changes, bounded pending
  bookkeeping, consumption credits and a Scene Graph pixel grab when available.
- `tst_globalmenu`: the desktop application menu's pure layout (titles, order,
  separators, enabled/checked per session state, the Quit shortcut), the action
  handling (`Connect`/`Disconnect` through the controller, input and statistics
  toggles, the dialog/quit requests) and the window lifecycle (attaching before
  the platform window exists, re-registration after hide/show, releasing the
  window). Where the session bus owns `com.canonical.AppMenu.Registrar` **and**
  the platform can announce a menu, it also reads the exported layout back over
  `com.canonical.dbusmenu.GetLayout` (labels, separators, toggle state, shortcut,
  and the state change after `setStatisticsVisible`), and on X11 it additionally
  asks the registrar which menu belongs to the window. Under the offscreen plugin
  it skips, because the application deliberately does not export a menu bar it
  cannot announce. The D-Bus part needs a real platform, so CTest (offscreen) only
  covers the skip; run it against a session with
  `QT_QPA_PLATFORM=wayland QT_QPA_PLATFORMTHEME=kde ./build/tests/tst_globalmenu`
  (or `xcb` on an X11 session) to exercise the export.
- `tst_session`: two-channel loopback TLS end-to-end registration/request/login,
  decode/acks/input, refusal statuses, timeouts, channel loss, unsupported packets,
  certificate mismatch, explicit insecure policy and backpressure final flush.
  Reconnect also checks cleared route/desktop state and no retained dictionary.
  TLS regressions additionally use `endpoint.crt` (localhost/127.0.0.1, without
  `ultraviewer`) to prove default endpoint-name authentication, and reject a
  mismatched relay route even when the registration override is valid.
- `app_self_test`: actual QML loading/viewer visibility, deterministic patch
  submission, byte-exact mirror and orange-patch window-pixel verification.
  If Qt cannot grab a window, pixel presentation is reported unchecked, not
  silently counted as GPU validation. Offscreen can use Qt's software Scene Graph;
  a successful grab does not establish hardware GPU performance. The summary line
  also reports the global menu as `menu=exported`, `menu=unavailable` or
  `menu=pending`; on a desktop that offers a menu but does not get it exported the
  test fails, so a silently empty panel widget is caught.
- `app_self_test_themed`: the same offscreen self-test with
  `QT_QPA_PLATFORMTHEME=kde`. A desktop theme can hand out a D-Bus menu bar on a
  platform without desktop services (offscreen), which used to make Qt crash while
  registering it; the run must stay clean and simply report no global menu. On
  hosts without such a theme the test is a plain pass-through.

The current native suite has **13 CTest targets** and eight image sequences.
Tests are deliberately not a proof of every vendor feature.

## Regenerate fixtures (development only)

Requires Python ≥3.10, Pillow, cryptography, libzstd and the `zstd` CLI:

```sh
python3 tools/generate_fixtures.py --reference reference --out tests/data/fixtures.json
ctest --test-dir build --output-on-failure
```

The generator executes upstream functions; all identities/passwords/traffic are
synthetic. It trains `test-dict.bin` and records its hash. JPEG comparisons allow
24 levels per channel for decoder differences; lossless RGB paths compare full
bytes/hashes. Legacy `canvas_hex` is **base64**, despite its name. Legacy JPEG
sample key `rgb555` holds decoded RGB888 values. Error entries need not have a hash.

Do not update expected bytes to conceal a native bug. Diagnose against the
reference first. The original generator's reversed RGB555 rows and hand-written
JPEG colours were corrected using actual reference decoding, and the reference
files themselves remain unchanged. No real `8bit.dat` is needed for these tests.

## Experimental registration profile (development only)

`tools/make_experimental_profile.py` drafts a 26-term registration profile for
this machine, using a published reference profile as the schema donor and
replacing every identity-bearing term (fingerprint, client password value,
machine name, MAC, user name, cached id). It is not installed by the package and
the application never generates these values on its own. The live service accepts
this profile's registration identity up to the partner-request phase (status-11
dry run below); a relay/login exchange with a real partner is still unproven.

## Sanitizers

```sh
cmake -S . -B build-sanitize -G Ninja -DCMAKE_CXX_COMPILER=g++-15 \
  -DCMAKE_BUILD_TYPE=Debug -DULTRAVIEW_ENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel 3
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-sanitize --output-on-failure
```

Leak detection is disabled for the whole Qt/platform process in this command;
it does not constitute a clean leak-sanitizer result. Qt/system libraries are
not instrumented by this project's build flag. Run on a compatible ASan runtime;
record actual diagnostics rather than counting a build alone as a sanitizer pass.

## UI review screenshots (development only)

```sh
QT_QPA_PLATFORM=offscreen ./build/ultraview --screenshot /tmp/ui-connect.png
QT_QPA_PLATFORM=offscreen ./build/ultraview \
  --screenshot /tmp/ui-settings.png --screenshot-dialog settings --screenshot-tab 1
```

`--screenshot <path>` renders the real window offscreen and saves the frame. The
optional `--screenshot-view viewer`, `--screenshot-statistics`,
`--screenshot-dialog settings|about` and `--screenshot-tab <n>` flags select a
surface first. They only set development properties on `Main.qml` and, for the
statistics panel, the global-menu state: no socket is opened and no profile is
read. See [interface](ui.md). These frames are how the 0.2.0 look was reviewed on
a headless machine; they do not cover hover/pressed/focus transitions or
tooltips, which need an interactive display session.

## Live connect probe (development only)

```sh
rm -rf /tmp/uv-probe && mkdir -p /tmp/uv-probe/ultraview
cp ~/.config/ultraview/client.ini /tmp/uv-probe/ultraview/   # optional: reuse the profile
XDG_CONFIG_HOME=/tmp/uv-probe QT_QPA_PLATFORM=offscreen ./build/ultraview \
  --connect-attempt 999999999
```

`--connect-attempt <partner-id>` runs exactly one attempt through the same
`ConnectionViewModel` path the Connect button uses (settings validation,
registration profile, dictionary, TLS policy and timeouts included) in a process
without a window, and prints the resulting state, desktop size and error text.
It takes **no credential from the caller**: it uses a placeholder password, so it
can only reach the registration and partner-request phases, which is what a first
live check needs — the service answers an unknown partner with the reference's
status 11. A real session belongs in the interface, where the password is typed.

The probe refuses to run unless `XDG_CONFIG_HOME` is redirected, because an
attempt writes a recent-connection entry and the credential-store preference; the
developer's own profile and keyring stay untouched. CTest covers only the
"unconfigured client refuses before any network I/O" case
(`app_connect_probe_unconfigured`); a live probe is a manual check and its output
is recorded in `verification.md`.

## Reference baseline

The upstream comparison snapshot is **not part of this repository** (it has no
LICENSE of its own; see [NOTICE.md](../NOTICE.md)). To run the baseline, check out
[caohoangphucs/ultraviewer-linux](https://github.com/caohoangphucs/ultraviewer-linux)
at commit `a0e090fe4de7ae31924a9a19e685c6f74bad74d3` into `reference/` first.

```sh
python3 -m unittest discover -s reference -p 'test_*.py'
```

Locally on Python 3.14: **40 run, 39 pass, one error**.
`test_viewer.ViewerTests.test_http_rejects_cross_origin_input` assumes the HTML
contains `const token='...'`; its regex returns None. This is an existing upstream
test/browser-shell mismatch, not fixed by the C++ port. Audio/sender-side reference
tests still run as upstream-only tests; their existence does not enable those
native features.

## Verification environment / exclusions

Verified locally on 2026-10-18, re-run on 2026-10-19 after adding the profile
loader test, again for the 0.2.0 interface refresh, again for the 0.2.1 desktop
application menu, again for the 0.2.2 live connect probe and once more for the
0.2.3 keyring build: **14/14 native CTest
targets pass** in the RelWithDebInfo build,
and **14/14 pass** in Debug with ASan/UBSan using the options above. The direct
40-patch QML self-test reports mirror/consumed/grab/pixel all `ok`, asserts that
every embedded theme icon is present and reports the global menu as
`menu=exported`/`unavailable`/`pending`. CPack generated a `.deb`; version 0.2.3 is
installed system-wide (`pkexec apt-get install`) and its `/usr/bin/ultraview`
passed the same smoke test and announced its menu to KWin on this Wayland session.
The interface was reviewed through offscreen `--screenshot` frames of each surface
and the exported menu through `WAYLAND_DEBUG` plus `com.canonical.dbusmenu` calls
(see `verification.md`). A manual live probe (`--connect-attempt`) reached the
service and read back the reference's status-11 refusal for an unknown partner.
See [verification record](verification.md).

Local development environment: Linux x86_64 Ubuntu 26.04, GCC 15.2, Qt 6.10.2,
OpenSSL 3.5.5, libzstd 1.5.7, CMake 4.2.3 and Ninja. `libsecret-1-dev` was added
on 2026-10-19, so the locally compiled backend links the platform Secret Service;
a build without it keeps the no-persistence path, and the packaging workflows
assert theirs.
GitHub CI configuration supplies Qt 6.8.3 because stock Ubuntu 24.04 is too old.
The workflow was executed on GitHub for the first time on 2026-10-19 (repository
`quytttb/ultraview-linux-client`): both matrix jobs — sanitizers `OFF` and `ON` —
completed successfully on `ubuntu-24.04` with the explicit Qt 6.8.3 SDK, and the
uploaded `LastTest.log` shows all 14 targets running. On that SDK the global menu
compiles out (private QtGui headers are a ≥6.9 distribution feature) and
`tst_globalmenu` reports 6 passed + 1 skipped (the D-Bus export probe needs a
session that offers a global menu), which is the intended graceful path; the
probe test `app_connect_probe_unconfigured` runs and refuses before any network
I/O.

Two further workflows package distribution-Qt artifacts on `ubuntu-26.04`
(`dev-build.yml` per main push, `build-release.yml` for `v*.*.*` tags with
checksums on the GitHub Release); both use the documented build, install
`libsecret-1-dev` with `-DULTRAVIEW_REQUIRE_SECRET_SERVICE=ON` so the artifact
must keep password storage, and run the suite as the gate — see
[packaging](packaging.md).

Not covered: a real Windows partner, live screen decoding with the vendor
dictionary in a session, a relay/login exchange, sustained external session,
clean-host package install, hardware/Wayland input shortcut behavior and
locked-keyring behavior. The registration and partner-request phases are
answered by the live service (status-11 dry run), a human confirmed the desktop
panel widget drawing the exported application menu, and the platform Secret
Service store/read/clear round trip ran against the session's gnome-keyring (see
`verification.md`). Follow `compatibility.md` before claiming end-to-end
interoperability.
