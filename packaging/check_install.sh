#!/bin/sh
# Installs into a temp prefix and fails if the file list is not exactly the expected one
# or the service unit does not point at the binary under that prefix.
set -eu
build_dir=${1:-build}
prefix=$(mktemp -d)
trap 'rm -rf "$prefix"' EXIT

cmake --install "$build_dir" --prefix "$prefix" > /dev/null

expected=$(cat <<EOF
bin/nexus
lib/systemd/user/nexus.service
share/applications/nexus.desktop
share/nexus/plugins/system/plugin.json
share/nexus/plugins/system/system.py
share/nexus/plugins/web/plugin.json
share/nexus/plugins/web/web.py
EOF
)
actual=$(cd "$prefix" && find . -type f | sed 's|^\./||' | sort)

if [ "$expected" != "$actual" ]; then
    echo "Installed files differ from the expected list (< expected, > actual):"
    printf '%s\n' "$expected" > "$prefix/.expected"
    printf '%s\n' "$actual" > "$prefix/.actual"
    diff "$prefix/.expected" "$prefix/.actual" || true
    exit 1
fi

unit="$prefix/lib/systemd/user/nexus.service"
if ! grep -qxF "ExecStart=\"$prefix/bin/nexus\"" "$unit"; then
    echo "nexus.service does not start $prefix/bin/nexus; it has:"
    grep '^ExecStart=' "$unit" || echo "(no ExecStart line)"
    exit 1
fi
echo "Install layout OK"
