#!/bin/sh
set -eu
[ "$(id -u)" -ne 0 ] || { echo 'Uninstall as the ordinary headset user.' >&2; exit 1; }
if [ -n "${XDG_RUNTIME_DIR:-}" ] && [ -e "$XDG_RUNTIME_DIR/frame-volume-mixer/instance.lock" ]; then
    command -v flock >/dev/null 2>&1 || { echo 'flock is required to check the running instance.' >&2; exit 1; }
    exec 9>"$XDG_RUNTIME_DIR/frame-volume-mixer/instance.lock"
    flock -n 9 || { echo 'Quit Frame Volume Mixer before uninstalling.' >&2; exit 1; }
fi
data_home=${XDG_DATA_HOME:-"$HOME/.local/share"}
rm -f "$data_home/applications/frame-volume-mixer.desktop"
for size in 48 128 256; do
    rm -f "$data_home/icons/hicolor/${size}x${size}/apps/frame-volume-mixer.png"
done
# Deliberately fixed project directory; never accept an arbitrary rm target.
rm -rf "$HOME/.local/opt/frame-volume-mixer"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$data_home/applications" >/dev/null 2>&1 || true
printf 'Frame Volume Mixer removed. Existing audio levels are unchanged.\n'
