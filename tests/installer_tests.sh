#!/bin/sh
# Isolated HOME and bundle; run as an unprivileged container user.
set -eu
bundle=${1:?Provide staged x64 bundle path}
root=$(mktemp -d /tmp/mixer-install-test.XXXXXX)
trap 'rm -rf "$root"' EXIT HUP INT TERM
export HOME="$root/home with spaces"
export XDG_DATA_HOME="$HOME/data with spaces"
export XDG_RUNTIME_DIR="$root/runtime"
mkdir -p "$HOME" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
mkdir -p "$XDG_DATA_HOME/applications"
printf 'unrelated' >"$XDG_DATA_HOME/applications/keep.desktop"
sh "$bundle/install.sh"
target="$HOME/.local/opt/frame-volume-mixer"
[ -x "$target/frame-volume-mixer" ] && [ -x "$target/launch.sh" ]
[ -f "$target/licenses/frame-mic-tuner/LICENSE" ]
[ -f "$target/licenses/openvr/LICENSE" ]
[ "$("$target/launch.sh" --version)" = "$("$bundle/frame-volume-mixer" --version)" ]
grep -F "Exec=\"$target/launch.sh\"" "$XDG_DATA_HOME/applications/frame-volume-mixer.desktop"
for size in 48 128 256; do [ -f "$XDG_DATA_HOME/icons/hicolor/${size}x${size}/apps/frame-volume-mixer.png" ]; done
# Installer/updater and uninstall must refuse a running resident instance.
mkdir -p "$XDG_RUNTIME_DIR/frame-volume-mixer"
(
    exec 8>"$XDG_RUNTIME_DIR/frame-volume-mixer/instance.lock"
    flock -x 8
    touch "$root/locked"
    exec sleep 20
) &
lock_pid=$!
for i in 1 2 3 4 5; do [ ! -e "$root/locked" ] || break; sleep .1; done
if sh "$bundle/install.sh"; then kill "$lock_pid"; exit 1; fi
if sh "$target/uninstall.sh"; then kill "$lock_pid"; exit 1; fi
kill "$lock_pid"; wait "$lock_pid" 2>/dev/null || true
sh "$bundle/install.sh"
sh "$target/uninstall.sh"
[ ! -e "$target" ]
[ ! -e "$XDG_DATA_HOME/applications/frame-volume-mixer.desktop" ]
[ -f "$XDG_DATA_HOME/applications/keep.desktop" ]
for size in 48 128 256; do [ ! -e "$XDG_DATA_HOME/icons/hicolor/${size}x${size}/apps/frame-volume-mixer.png" ]; done
printf 'PASS install/update/uninstall, spaces, dependency notices, running-instance refusal, unrelated-file preservation\n'
