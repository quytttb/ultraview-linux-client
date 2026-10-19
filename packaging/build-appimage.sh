#!/usr/bin/env bash
# Requires locally installed linuxdeploy + linuxdeploy-plugin-qt; never downloads
# or executes a remote binary. Pin and verify those tools yourself before use.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=$(realpath "${1:-$root/build}")
command -v linuxdeploy >/dev/null || { echo 'Install a verified linuxdeploy in PATH first.' >&2; exit 1; }
command -v linuxdeploy-plugin-qt >/dev/null || { echo 'Install linuxdeploy-plugin-qt in PATH first.' >&2; exit 1; }
: "${QMAKE:?Set QMAKE to the qmake executable of the Qt used for this build}"
stage=$(mktemp -d "$build/appimage-stage.XXXXXX")
# DESTDIR is confined to a fresh directory; no existing files are removed.
DESTDIR="$stage" cmake --install "$build" --prefix /usr --strip
export QML_SOURCES_PATHS="$root/qml"
export EXTRA_QT_PLUGINS="imageformats;tls"
export OUTPUT="${OUTPUT:-$build/Ultraview-$(uname -m).AppImage}"
linuxdeploy --appdir "$stage" \
    --executable "$stage/usr/bin/ultraview" \
    --desktop-file "$root/packaging/ultraview.desktop" \
    --icon-file "$root/packaging/ultraview.svg" \
    --plugin qt --output appimage
echo "AppDir retained for inspection: $stage"
echo "Run: QT_QPA_PLATFORM=offscreen $OUTPUT --appimage-extract-and-run --self-test"
