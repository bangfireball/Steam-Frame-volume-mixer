# Frame Volume Mixer

Small native Steam Frame dashboard utility for master output and per-application
playback volume/mute. Open it from **SteamVR dashboard → `+` → Frame Volume Mixer**. Launching
again opens the resident panel; it does not start a second mixer.

**Status:** basic native launch/UI/audio control owner-confirmed on Frame. Version 0.2.0's master controls and spatializer matching are locally tested; headset retest pending.
See [design.md](design.md) and [validation.md](docs/validation.md).

**Important:** quitting resets **all currently exposed writable app playback streams**
to 100%, including streams you did not adjust. Master volume and all mute states
are left unchanged. Hiding the dashboard does not reset anything.

- [Install and update](#install-on-frame)
- [Using the mixer](#using-the-mixer)
- [Troubleshooting](#troubleshooting)
- [Diagnostics](#diagnostics--ui-preview)
- [Build and test](#build-and-test)

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

### 1. Obtain the ARM64 bundle

You need `frame-volume-mixer-arm64-0.2.0.tar.gz` and its adjacent `.sha256` file.
The build creates them in `out/packages/`; generated binaries are **not committed to
this repository**, and pushing source does not publish a downloadable GitHub release.
Build them using the [Docker workflow](#docker-build-x64-host-to-arm64), or obtain
both files from a trusted distributor. Do not run `packaging/install.sh` directly
from a source checkout: it requires a complete staged bundle.

Copy both files to the headset, for example to `~/Downloads`, using your usual file
transfer method. If SSH is already configured, this optional command runs on the
**build computer**; replace `FRAME_USER` and `FRAME_IP` with your actual values and
ensure the headset's `~/Downloads` directory exists:

```sh
scp out/packages/frame-volume-mixer-arm64-0.2.0.tar.gz \
    out/packages/frame-volume-mixer-arm64-0.2.0.tar.gz.sha256 \
    FRAME_USER@FRAME_IP:~/Downloads/
```

### 2. Verify, extract and install

Run these commands **on the headset**, as its ordinary logged-in user, **without
sudo**. Quit any existing mixer first using its Quit button.

```sh
cd ~/Downloads &&
awk '{print $1 "  frame-volume-mixer-arm64-0.2.0.tar.gz"}' \
    frame-volume-mixer-arm64-0.2.0.tar.gz.sha256 | sha256sum -c - &&
tar -xzf frame-volume-mixer-arm64-0.2.0.tar.gz &&
sh frame-volume-mixer-arm64-0.2.0/install.sh
```

The checksum check must report `frame-volume-mixer-arm64-0.2.0.tar.gz: OK` before
extraction/install proceeds. The command uses the checksum's first field because
the build's checksum file records an `out/packages/` path, not the download folder.
A matching checksum detects transfer corruption; it does not authenticate an
untrusted distributor.

This installs under `~/.local/opt/frame-volume-mixer` and adds the user `.desktop`
entry plus launcher icons. It requires the installed OS's **libpulse.so.0,
libcairo.so.2, libvulkan.so.1**, system fonts, and working SteamVR/OpenVR runtime.
The installer verifies the executable's architecture/library loading with
`--version`. It does not install OS packages or start/restart services.

### 3. Launch

In the headset's SteamVR session, open **dashboard → `+` → Frame Volume Mixer**.
Launching again shows the existing panel rather than starting a second instance.
For a manual launch from a terminal already in the headset user's session:

```sh
sh ~/.local/opt/frame-volume-mixer/launch.sh
```

### Update or uninstall

**Update:** Quit the running mixer, verify/extract the new ARM64 bundle, then run
that bundle's `install.sh` as the same ordinary user. It replaces the existing
installation; uninstalling first is unnecessary. Substitute the new version in
the filenames above. Keep the extracted bundle outside the installation directory.

**Uninstall:** Quit the mixer, then run:

```sh
sh ~/.local/opt/frame-volume-mixer/uninstall.sh
```

Uninstall removes this utility's installed files, launcher and icons. It does not
reset audio levels itself or remove unrelated programs/services.

Launch requires the headset user's `XDG_RUNTIME_DIR` (real, owned, mode 0700) and
access to that user's audio/VR session. Running over SSH alone is not proof that
launching from `+` works. If missing from `+`, verify the desktop entry and icon
files, then investigate launcher refresh on the actual runtime; don't patch Steam.

## Using the mixer

1. Start the game/browser/audio sources you want to control, then open the mixer.
   Only playback streams currently exposed by the local audio server can be listed;
   it cannot launch apps or control an app that has no exposed stream.
2. Use **Master — Default output** for the overall level of that output. Use the
   application rows to balance individual streams underneath it. Drag a slider
   with the dashboard pointer; **Mute/Muted** toggles mute independently of gain.
3. **Previous/Next** (or scroll) pages through five application rows. The master
   stays at the top. Paused/corked streams remain listed when the server retains them.
4. Normally leave **Internals: off**. Toggle it on to inspect matched spatializer
   stages. These affect the same audio path as their parent app, not another app.
   A parent-row warning such as **Spatializer muted** or **Spatializer 35%** means
   that downstream stage is also limiting the sound.
5. **Reconnect** refreshes audio discovery after a connection problem; it does not
   restart the sound server or reset volumes.
6. Switching dashboard tabs or dismissing the dashboard leaves the mixer running.
   To exit, select **Quit**, then **Quit now?** within four seconds. The native panel
   close event also exits and runs the all-stream reset described above.

Externally set levels above 100% are displayed, but dragging a slider only offers
0–100%. The mixer has no persistent app profiles, output-device selector, microphone
controls, or autostart service.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| No `+` launcher entry | Check `~/.local/share/applications/frame-volume-mixer.desktop` (or `$XDG_DATA_HOME/applications`) and the corresponding hicolor icons. Try the installed `launch.sh` in the active headset session. Launcher refresh still needs native-runtime validation; do not patch Steam. |
| Missing `XDG_RUNTIME_DIR`, or audio/VR unavailable over SSH | Launch from the headset session. The runtime directory must belong to that user with mode 0700, and the audio/VR session must be accessible. Do not fix this with sudo, world-writable permissions, or guessed runtime paths. |
| Waiting for SteamVR | Start/use the normal headset SteamVR session. The mixer retries but does not start SteamVR itself. |
| No app rows | Start playback and run the read-only `--list` diagnostic below in the same user's audio session. A PC-streamed mix cannot be separated into its original PC applications. |
| Multiple rows for an app | Real separate streams remain separate. Uncertain spatializer matches deliberately remain visible; only confirmed duplicates are hidden. |
| Quiet despite an app at 100% | Check master level/mute, app mute, and the spatializer warning/Internals view. Another downstream stage or a different output route may still limit sound. |
| Master unavailable or not affecting the expected device | The master targets only the server's current default output. Select the intended output in the normal system controls, then reconnect if needed. If available, `pactl info` and `pactl list short sinks` are read-only ways to inspect that route. |
| Slider unavailable or a change rejected | The server may not expose a writable volume control, or the stream/output may have disappeared. Reconnect and check the displayed error; do not restart audio services as an installation step. |
| App still quiet after exit | Reset is best-effort and only covers streams exposed at shutdown. Mutes/master remain unchanged; stopped apps, server loss, crashes or SIGKILL can leave restoration to the audio server. |
| Installer cannot run the build | Confirm ARM64 and the runtime libraries listed above. An x64 build will not run natively on Frame. Do not install development packages or modify the OS just to bypass this check. |

For a problem report, include the mixer version, how it was launched, which controls
failed, and relevant console errors. Review/redact application/media names before
sharing diagnostics. Do not share `.env`, credentials or raw private probe dumps.

## Diagnostics / UI preview

These installed-program commands can run from any directory:

```sh
~/.local/opt/frame-volume-mixer/frame-volume-mixer --list              # read-only streams, no VR
~/.local/opt/frame-volume-mixer/frame-volume-mixer --version
~/.local/opt/frame-volume-mixer/frame-volume-mixer --help
~/.local/opt/frame-volume-mixer/frame-volume-mixer --preview mixer.ppm # fake screenshot, no audio/VR
~/.local/opt/frame-volume-mixer/frame-volume-mixer --mock              # fake dashboard, requires VR
```

`--list` includes raw playback streams, including normally hidden spatializers;
it is not a master-output report. Success with no streams produces no rows; an
unavailable audio server exits with code 2. A preview is a PPM image, not a PNG.
`--mock` shares the normal single-instance identity: quit a real running mixer
before trying it, otherwise the existing panel is simply shown.
For an uninstalled build, substitute `out/linux-x64/frame-volume-mixer` (or the
appropriate build path) for the installed executable.

Console diagnostics contain state/errors, not captured audio. `--list` includes
application and media names; review/redact before sharing. Logs are not persisted
by the application. It retries absent audio/runtime connections at a bounded rate.

## Build and test

Clone the source and run the following build commands from the repository root:

```sh
git clone https://github.com/bangfireball/Steam-Frame-volume-mixer.git
cd Steam-Frame-volume-mixer
```

CMake 3.22+ and a C++20 compiler are required; native builds also fetch the pinned
OpenVR SDK using Git/network access. Windows portable tests skip the audio/VR
SDK dependencies. Build on a development computer, not by modifying the headset OS.
Native audio/dashboard builds target Linux; Windows supports portable model tests
only. ARM64 is the headset architecture; Linux x64 is useful for local development.

### Linux x64 (also useful for desktop diagnostics)

Debian development packages: `build-essential cmake ninja-build git pkg-config
libpulse-dev libcairo2-dev libvulkan-dev` and a system font. For isolated integration
tests also install `pulseaudio pulseaudio-utils`.

```sh
cmake -S . -B out/linux-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/linux-x64
ctest --test-dir out/linux-x64 --output-on-failure
```

### Docker build (x64 host to ARM64)

Use Docker with **Linux containers** on an x86_64 development host. The image
provides cross-compilers, ARM64 dependencies, QEMU and the private test server.
Do not mount your real audio sockets into the test container.

```sh
docker build -t frame-volume-mixer-build:latest -f scripts/Dockerfile .
docker run --rm -v "$PWD:/work" frame-volume-mixer-build:latest sh scripts/build.sh
# Private x64 integration test (not registered in CTest):
docker run --rm --user tester -v "$PWD:/work" frame-volume-mixer-build:latest \
  sh scripts/test-private-pulse.sh out/linux-x64
# Same backend tests with the ARM64 executable under QEMU:
docker run --rm --user tester -e MIXER_QEMU=1 -v "$PWD:/work" \
  frame-volume-mixer-build:latest sh scripts/test-private-pulse.sh out/linux-arm64
```

On **Git Bash / Windows**, avoid MSYS path rewriting and use an absolute Windows
bind source. In Git Bash, `cygpath` derives it from your actual checkout directory:

```sh
docker build -t frame-volume-mixer-build:latest -f scripts/Dockerfile .
MSYS_NO_PATHCONV=1 docker run --rm \
  --mount "type=bind,source=$(cygpath -m "$PWD"),target=/work" \
  frame-volume-mixer-build:latest sh scripts/build.sh
MSYS_NO_PATHCONV=1 docker run --rm --user tester \
  --mount "type=bind,source=$(cygpath -m "$PWD"),target=/work" \
  frame-volume-mixer-build:latest sh scripts/test-private-pulse.sh out/linux-x64
MSYS_NO_PATHCONV=1 docker run --rm --user tester -e MIXER_QEMU=1 \
  --mount "type=bind,source=$(cygpath -m "$PWD"),target=/work" \
  frame-volume-mixer-build:latest sh scripts/test-private-pulse.sh out/linux-arm64
```

`scripts/build.sh` builds/tests Linux x64, cross-builds ARM64, runs portable ARM64
tests/version/preview under QEMU, and creates a release tarball under `out/packages`.
QEMU/null sinks do not validate VR rendering, physical audibility, or Frame load.
The image's Debian package versions are not frozen; OpenVR is pinned. Record image
identity/tool versions alongside release validation.

### Windows portable tests only

Use Git Bash with MinGW GCC (C++20 support), CMake and Ninja available on `PATH`.
These are the tested Windows tools; the project does not provide an MSVC workflow.

```sh
cmake -S . -B out/windows-core -G Ninja -DMIXER_NATIVE=OFF \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
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
autostart service is included.

## Licensing

Third-party components retain their own licenses; see [third-party notices](THIRD_PARTY.md).
No separate project-wide license is currently declared for the original mixer source.
