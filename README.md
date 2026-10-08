# Frame Volume Mixer

Small native Steam Frame dashboard utility for master output and per-application
playback volume/mute. Open it from **SteamVR dashboard → `+` → Frame Volume Mixer**. Launching
again opens the resident panel; it does not start a second mixer.

**Status:** basic native launch/UI/audio control owner-confirmed on Frame. Version 0.2.0's master controls and spatializer matching are locally tested; headset retest pending.
See [design.md](design.md) and [validation.md](docs/validation.md).

## What it does

- Lists local PulseAudio/PipeWire playback streams with application/media labels.
- Volume 0–100% (100% = 0 dB), independent mute, preserved channel balance.
- Reflects other mixers' edits; handles stream creation/removal and reconnects.
- Master volume/mute for the current default output, with live device/external-change tracking.
- Confirmed internal spatializer rows hidden by default; **Internals** toggles them back on.
- Dashboard pointer sliders, five-row paging, reconnect, confirmed Quit.
- No audio capture, default-device changes, service restarts, or microphone controls.

One application may have several real playback streams. General app grouping and
persistent rules are deferred. A Frame spatializer is hidden only when its current
routing, shared filter/client metadata and generated node names match a single
identified app stream. Uncertain/independent streams remain visible. Missing app
labels fall back to media/node metadata (e.g. Valve VRLink), not indiscriminate hiding.
Hidden filter attenuation/mute is noted on the parent row; use **Internals** to inspect
or adjust it. Those streams remain included in the exit reset. Browser tabs are separate only if their
streams are separate. A PC-streamed mix cannot be split into its original PC apps;
this program controls only streams visible on the headset.

Switching tabs or dismissing the dashboard hides the panel but keeps the mixer running.
Use **Quit → Quit now?** or the panel's native close button to stop the process.
On normal exit, **all currently exposed, writable playback streams are reset to 100%**
(0 dB, preserving channel balance). This includes streams not adjusted by the mixer.
Mute states and headset master volume remain unchanged. SteamVR quit requests and
SIGINT/SIGTERM follow the same reset path. The reset waits for server acknowledgements,
with a two-second best-effort deadline; an unavailable server can prevent restoration.
Crashes, SIGKILL, and power loss cannot execute this cleanup. `--list`, `--preview`,
`--mock`, and secondary launches do not reset real volumes.
Server/WirePlumber restoration determines later stream levels; the mixer does not
continuously enforce saved app rules.

## Master output controls

The top row targets the audio server's **current default output**, not every device.
Volume is 0–100% (no boost), with channel balance and a separate mute state preserved.
Changing outputs externally rebinds the row and invalidates stale queued controls.
When default output discovery is unavailable, controls are disabled rather than
silently changing an app or another sink. Adjusting master does not change individual
app gains. Quitting leaves the current master volume and mute untouched.
On Frame this is expected to be the headset speaker output; validate the actual
route/synchronization with native headset volume controls on device.

## Install on Frame

Extract `frame-volume-mixer-arm64-0.2.0.tar.gz`, then, as the ordinary headset user:

```sh
sh frame-volume-mixer-arm64-0.2.0/install.sh
```

This installs under `~/.local/opt/frame-volume-mixer` and adds the user `.desktop`
entry plus launcher icons. It requires the installed OS's **libpulse.so.0,
libcairo.so.2, libvulkan.so.1**, system fonts, and working SteamVR/OpenVR runtime.
The installer verifies the executable's architecture/library loading with
`--version`. It does not install OS packages or start/restart services.

Quit the mixer before updating/uninstalling. Uninstall:

```sh
sh ~/.local/opt/frame-volume-mixer/uninstall.sh
```

Launch requires the headset user's `XDG_RUNTIME_DIR` (real, owned, mode 0700) and
access to that user's audio/VR session. Running over SSH alone is not proof that
launching from `+` works. If missing from `+`, verify the desktop entry and icon
files, then investigate launcher refresh on the actual runtime; don't patch Steam.

## Diagnostics / UI preview

```sh
./frame-volume-mixer --list                 # read-only stream list, no VR needed
./frame-volume-mixer --preview mixer.ppm    # fake UI screenshot; no audio/VR
./frame-volume-mixer --mock                 # fake dashboard; no audio writes
./frame-volume-mixer --version
```

Console diagnostics contain state/errors, not captured audio. `--list` includes
application and media names; review/redact before sharing. Logs are not persisted
by the application. It retries absent audio/runtime connections at a bounded rate.

## Build and test

### Linux x64 (also useful for desktop diagnostics)

Debian development packages: `build-essential cmake ninja-build git pkg-config
libpulse-dev libcairo2-dev libvulkan-dev` and a system font. For isolated integration
tests also install `pulseaudio pulseaudio-utils`.

```sh
cmake -S . -B out/linux-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/linux-x64
ctest --test-dir out/linux-x64 --output-on-failure
```

### Reproducible-toolchain Docker workflow (x64 host → ARM64)

```sh
docker build -t frame-volume-mixer-build -f scripts/Dockerfile .
docker run --rm -v "$PWD:/work" frame-volume-mixer-build sh scripts/build.sh
# Isolated null-sink integration test, not the real user's sound server:
docker run --rm --user tester -v "$PWD:/work" frame-volume-mixer-build \
  sh scripts/test-private-pulse.sh
```

On **Git Bash / Windows**, avoid MSYS path rewriting and use an absolute Windows
bind source:

```sh
MSYS_NO_PATHCONV=1 docker run --rm \
  --mount type=bind,source=C:/Projects/steam_frame_volume_mixer,target=/work \
  frame-volume-mixer-build sh scripts/build.sh
```

`scripts/build.sh` builds/tests Linux x64, cross-builds ARM64, runs portable ARM64
tests/version/preview under QEMU, and creates a release tarball under `out/packages`.
QEMU/null sinks do not validate VR rendering, physical audibility, or Frame load.
The image's Debian package versions are not frozen; OpenVR is pinned. Record image
identity/tool versions alongside release validation.

### Windows portable tests only

```sh
cmake -S . -B out/windows-core -G Ninja -DMIXER_NATIVE=OFF
cmake --build out/windows-core
ctest --test-dir out/windows-core --output-on-failure
```

This does not build a Windows audio mixer or dashboard.

## Hardware acceptance still required

Basic launch and stream volume adjustment are owner-confirmed. Read-only Frame
inspection identified the Firefox Unknown row as WirePlumber's spatializer playback;
see [investigation](docs/frame-audio-duplicates.md).
On-device follow-up: spatializer matching and Internals toggle; master output volume/
mute and external edits/route changes; exit restores all exposed streams to 100%
without changing mute/master; ordinary app changes leave unrelated streams alone;
`+` discovery, pointer input, no flicker, repeated launch, pause/new tracks,
dashboard/SteamVR restart, sleep/wake, installation compatibility, and CPU/memory/
game frame timing. No native Quick Settings injection or
autostart service is included. See [third-party notices](THIRD_PARTY.md).
