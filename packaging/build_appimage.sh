#!/bin/sh
# Builds Nexus-<version>-x86_64.AppImage from an already built tree (Release, any prefix).
# linuxdeploy, its Qt plugin and the AppImage runtime are pinned by release tag and SHA256; a mismatch aborts.
# Usage: build_appimage.sh <build-dir> <output-dir>
set -eu
build_dir=$(cd "${1:?usage: build_appimage.sh <build-dir> <output-dir>}" && pwd)
out_dir=$(mkdir -p "${2:?usage: build_appimage.sh <build-dir> <output-dir>}" && cd "$2" && pwd)

linuxdeploy_tag=1-alpha-20250213-2
linuxdeploy_sha=4648f278ab3ef31f819e67c30d50f462640e5365a77637d7e6f2ad9fd0b4522a
qt_plugin_tag=1-alpha-20250213-1
qt_plugin_sha=15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724
runtime_tag=20251108
runtime_sha=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

fetch() { # <owner/repo> <tag> <file> <sha256>
    curl -fsSL -o "$work/$3" "https://github.com/$1/releases/download/$2/$3"
    echo "$4  $work/$3" | sha256sum -c - > /dev/null
    chmod +x "$work/$3"
}
fetch linuxdeploy/linuxdeploy "$linuxdeploy_tag" linuxdeploy-x86_64.AppImage "$linuxdeploy_sha"
fetch linuxdeploy/linuxdeploy-plugin-qt "$qt_plugin_tag" linuxdeploy-plugin-qt-x86_64.AppImage "$qt_plugin_sha"
fetch AppImage/type2-runtime "$runtime_tag" runtime-x86_64 "$runtime_sha"

DESTDIR="$work/AppDir" cmake --install "$build_dir" --prefix /usr > /dev/null

# runners have no FUSE, so the tools unpack themselves instead of mounting
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$work:$PATH"
export LDAI_RUNTIME_FILE="$work/runtime-x86_64"
LINUXDEPLOY_OUTPUT_VERSION=$("$build_dir/nexus" --version | cut -d' ' -f2)
export LINUXDEPLOY_OUTPUT_VERSION
# linuxdeploy-plugin-qt bundles only xcb unless told: add the Wayland platform plugins and their shell integration
# (in this plugin version the "waylandcompositor" module is what deploys the client shell/decoration plugins)
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so"
export EXTRA_QT_MODULES="waylandcompositor"
cd "$out_dir"
"$work/linuxdeploy-x86_64.AppImage" --appdir "$work/AppDir" --plugin qt \
    --desktop-file "$work/AppDir/usr/share/applications/nexus.desktop" \
    --icon-file "$work/AppDir/usr/share/icons/hicolor/scalable/apps/nexus.svg" \
    --output appimage
ls "$out_dir"/*.AppImage
