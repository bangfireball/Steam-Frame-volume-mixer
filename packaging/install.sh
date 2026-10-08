#!/bin/sh
set -eu
umask 077
[ "$(id -u)" -ne 0 ] || { echo 'Install as the ordinary headset user, not root.' >&2; exit 1; }
bundle=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
install_dir="$HOME/.local/opt/frame-volume-mixer"
data_home=${XDG_DATA_HOME:-"$HOME/.local/share"}
applications="$data_home/applications"
# These characters require multiple desktop-entry escaping layers. Fail clearly rather
# than producing a malformed launcher. Ordinary paths, including spaces, are supported.
case "$HOME$data_home" in *'"'*|*'\'*|*'`'*|*'$'*|*'%'*|*'
'*) echo 'Unsupported launcher path characters in HOME/XDG_DATA_HOME.' >&2; exit 1;; esac
for file in frame-volume-mixer frame-volume-mixer.vrmanifest launch.sh uninstall.sh README.md design.md THIRD_PARTY.md docs/validation.md; do
    [ -f "$bundle/$file" ] || { echo "Incomplete bundle: $file" >&2; exit 1; }
done
for size in 48 128 256; do
    [ -f "$bundle/icons/frame-volume-mixer-$size.png" ] || { echo 'Incomplete icon set.' >&2; exit 1; }
done
[ -d "$bundle/licenses" ] || { echo 'Missing dependency licenses.' >&2; exit 1; }
# Checks ELF architecture and shared-library availability without initializing VR/audio.
"$bundle/frame-volume-mixer" --version >/dev/null || {
    echo 'Cannot run this build. Check ARM64 architecture and libpulse, libcairo, libvulkan dependencies.' >&2
    exit 1
}
if [ -n "${XDG_RUNTIME_DIR:-}" ] && [ -e "$XDG_RUNTIME_DIR/frame-volume-mixer/instance.lock" ]; then
    command -v flock >/dev/null 2>&1 || { echo 'flock is required to check the running instance.' >&2; exit 1; }
    exec 9>"$XDG_RUNTIME_DIR/frame-volume-mixer/instance.lock"
    flock -n 9 || { echo 'Quit Frame Volume Mixer before updating.' >&2; exit 1; }
fi
mkdir -p "$install_dir" "$applications"
stage=$(mktemp -d "$install_dir/.install.XXXXXX")
trap 'rm -rf "$stage"' EXIT HUP INT TERM
cp "$bundle/frame-volume-mixer" "$bundle/frame-volume-mixer.vrmanifest" "$bundle/launch.sh" "$bundle/uninstall.sh" "$bundle/README.md" "$bundle/design.md" "$bundle/THIRD_PARTY.md" "$stage/"
cp -R "$bundle/licenses" "$stage/licenses"
cp -R "$bundle/docs" "$stage/docs"
chmod 755 "$stage/frame-volume-mixer" "$stage/launch.sh" "$stage/uninstall.sh"
for file in frame-volume-mixer frame-volume-mixer.vrmanifest launch.sh uninstall.sh README.md design.md THIRD_PARTY.md; do
    mv -f "$stage/$file" "$install_dir/$file"
done
mkdir -p "$install_dir/licenses"
cp -R "$stage/licenses/." "$install_dir/licenses/"
mkdir -p "$install_dir/docs"
cp -R "$stage/docs/." "$install_dir/docs/"
for size in 48 128 256; do
    destination="$data_home/icons/hicolor/${size}x${size}/apps"
    mkdir -p "$destination"
    cp "$bundle/icons/frame-volume-mixer-$size.png" "$destination/frame-volume-mixer.png"
    chmod 644 "$destination/frame-volume-mixer.png"
done
launcher="$applications/frame-volume-mixer.desktop"
printf '[Desktop Entry]\nType=Application\nName=Frame Volume Mixer\nComment=Per-application playback volume\nExec="%s/launch.sh"\nIcon=frame-volume-mixer\nTerminal=false\nCategories=AudioVideo;Audio;Mixer;\n' "$install_dir" >"$stage/launcher"
chmod 644 "$stage/launcher"
mv -f "$stage/launcher" "$launcher"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$applications" >/dev/null 2>&1 || true
printf 'Installed: %s\nOpen SteamVR dashboard → + → Frame Volume Mixer.\n' "$install_dir"
