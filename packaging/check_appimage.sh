#!/bin/sh
# Runs a built AppImage: --version works, its bundled plugins are found from $APPDIR with no user or
# system plugin directory around, and the Qt Wayland platform plugin is bundled.
set -eu
image=$(realpath "${1:?usage: check_appimage.sh <file.AppImage>}")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export APPIMAGE_EXTRACT_AND_RUN=1

"$image" --version | grep -q '^nexus [0-9]'
# Empty XDG dirs and an absolute-but-empty data dir list: only $APPDIR can supply the plugin.
out=$(HOME="$work/home" XDG_DATA_HOME="$work/data" XDG_CONFIG_HOME="$work/config" \
    XDG_DATA_DIRS="$work/none" QT_QPA_PLATFORM=offscreen "$image" query "web hello")
echo "$out" | grep -q 'provider: web'

# Wayland sessions need the Wayland platform plugin and its xdg-shell integration inside the image.
(cd "$work" && "$image" --appimage-extract > /dev/null)
for f in plugins/platforms/libqwayland-generic.so plugins/wayland-shell-integration/libxdg-shell.so; do
    [ -f "$work/squashfs-root/usr/$f" ] || { echo "AppImage lacks usr/$f" >&2; exit 1; }
done
echo "AppImage OK"
