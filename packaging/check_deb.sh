#!/bin/sh
# Inspects a built .deb: exact file list, python3 dependency, shlibdeps-derived Qt/SQLite/X11 deps,
# and the Qt Wayland plugin as a recommendation.
set -eu
deb=${1:?usage: check_deb.sh <file.deb>}

expected=$(cat <<EOF
usr/bin/nexus
usr/lib/systemd/user/nexus.service
usr/share/applications/nexus.desktop
usr/share/icons/hicolor/scalable/apps/nexus.svg
usr/share/nexus/plugins/system/plugin.json
usr/share/nexus/plugins/system/system.py
usr/share/nexus/plugins/web/plugin.json
usr/share/nexus/plugins/web/web.py
EOF
)
# dpkg-deb -c prints "perms owner size date time ./path"; keep regular files only
actual=$(dpkg-deb -c "$deb" | awk '$1 ~ /^-/ {print $6}' | sed 's|^\./||' | sort)
if [ "$expected" != "$actual" ]; then
    echo "Package files differ from the expected list (< expected, > actual):"
    printf '%s\n' "$expected" > "$deb.expected"
    printf '%s\n' "$actual" > "$deb.actual"
    diff "$deb.expected" "$deb.actual" || true
    rm -f "$deb.expected" "$deb.actual"
    exit 1
fi

# Group- or world-writable entries would let other local users swap files run as root or by every user.
writable=$(dpkg-deb -c "$deb" | awk '$1 ~ /^.....w|^........w/ {print $6}')
if [ -n "$writable" ]; then
    echo "Package has group- or world-writable entries:"
    echo "$writable"
    exit 1
fi

depends=$(dpkg-deb -f "$deb" Depends)
recommends=$(dpkg-deb -f "$deb" Recommends)
for want in python3 libsqlite3-0 libx11-6 libqt; do
    case "$depends" in
        *"$want"*) ;;
        *) echo "Depends is missing $want: $depends"; exit 1 ;;
    esac
done
case "$recommends" in
    *qtwayland5* | *qt6-wayland*) ;;
    *) echo "Recommends has no Qt Wayland plugin: $recommends"; exit 1 ;;
esac
echo "Deb OK"
