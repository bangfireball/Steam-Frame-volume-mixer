# Validation record

Basic native launch/UI/volume control is owner-confirmed on Steam Frame. This file
separates that report from automated evidence and remaining hardware gates.

## Local evidence — 2026-10-07

Toolchain: Docker Debian bookworm multiarch image `frame-volume-mixer-build`,
current 0.2.0 image config `fcfb486676e2d9664b5ee8f8a1e705dcc1048b1238a70107de2ee569cc002291`;
GCC 12.2 x64 / AArch64 cross compiler; CMake 3.25.1; libpulse 16.1;
Cairo 1.16.0; Vulkan headers/loader 1.3.239. Windows portable tests use MinGW
GCC 16.2. OpenVR SDK is pinned as recorded in `THIRD_PARTY.md`.

| Validation | Result | Scope |
| --- | --- | --- |
| Linux x64 Release build and CTest | PASS | Model (1,225 checks), private instance lock/IPC |
| Windows portable build and CTest | PASS | Model only, no Windows dashboard/audio backend |
| Linux ARM64 Release cross-build | PASS | Actual AArch64 ELF executable |
| ARM64 model / instance tests under QEMU | PASS | Portable behavior and Linux IPC, not a headset |
| ARM64 version / mock panel preview under QEMU | PASS | Dynamic dependency loading and CPU UI rendering |
| Private PulseAudio null-sink integration, x64 | PASS | Apps/reset, master controls/default switching, matched spatializers/route changes, external edits, stale-target rejection |
| Private PulseAudio null-sink integration, ARM64 under QEMU | PASS | Same backend tests with AArch64 client and x64 private server |
| Isolated installer/update/uninstaller with x64 test bundle | PASS | Spaces in home/data paths, launch command, icons/licenses, running-instance refusal, unrelated file preserved |
| x64 CPU panel preview visually inspected | PASS | Readable mock rows/buttons; no proof of VR texture/input behavior |
| x64 non-PIE ASan/UBSan model/IPC and private audio tests | PASS | No reported sanitizer finding in those runs |

Current release bundle: `out/packages/frame-volume-mixer-arm64-0.2.0.tar.gz`;
SHA-256 is in its adjacent `.sha256` file. Current UI preview: `out/mixer-020.png`.
Generated outputs are ignored by Git.

Important harness findings:

- Initial PIE ASan runs were unstable (a timed-out run and repeated
  `AddressSanitizer:DEADLYSIGNAL` without a usable stack); the no-PIE sanitizer
  build passes. The underlying PIE sanitizer/harness failure is not diagnosed
  or presented as a proven application fix. Release builds remain PIE.
- QEMU must use the distro's ARM64 loader (`-L /`) and multiarch libraries,
  not the older cross-toolchain sysroot loader. Mixing those initially hung
  the ARM64 private audio test; matching the distro loader passes. The build
  and integration scripts now encode that configuration and bounded timeouts.
- These are PulseAudio protocol tests, not a PipeWire-on-Frame reproduction.
  Vulkan uploads, native runtime lifecycle, audibility, and performance still
  require the hardware gates below.

Reproduce:

```sh
sh scripts/build.sh
sh scripts/test-private-pulse.sh out/linux-x64
MIXER_QEMU=1 sh scripts/test-private-pulse.sh out/linux-arm64
# Run installer tests as an ordinary user with a staged x64 test bundle:
sh tests/installer_tests.sh /work/out/stage/frame-volume-mixer-x64-test
```

Run private integration tests as `tester` inside the build container with no host
audio socket mounted. For sanitizer builds, use C++ flags
`-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie` and executable
linker flags `-fsanitize=address,undefined -no-pie`.

## Owner report and 0.1.1 follow-up

The owner reports that the mixer works on Frame and playback volume controls work.
Chromium and Unknown rows sometimes control the same audio; stream metadata/grouping
remains open. Version 0.1.0 intentionally retained gains on Quit, which left an app
quiet until the mixer was reopened. At the owner's request, 0.1.1 resets all exposed
writable stream gains to 100% on normal exit, preserving mute/channel balance/master.

Local x64 and ARM64/QEMU private Pulse tests now additionally pass:

- All-stream shutdown reset, including a stream lowered outside the interactive mixer.
- Final queued/in-flight slider commands cannot override the shutdown reset.
- Mute states and master sink volume remain unchanged.
- A read-only backend's destruction does not reset any volume.
- Reset when the audio server is unavailable remains bounded (two-second deadline).
- Updated x64 non-PIE ASan/UBSan tests and isolated installer/update/uninstall tests pass.

Normal Quit, the OpenVR panel close event, SteamVR quit requests and handled SIGINT /
SIGTERM all lead to this cleanup. Native close-button and owner-observed restored
volume require physical retest. SIGKILL, crashes and power loss cannot run cleanup;
server loss can prevent a best-effort reset. No headset was changed remotely.

## Spatializer matching and master controls — 0.2.0

Approved additions implemented locally; no updated binary was installed remotely.
The read-only Firefox snapshot is documented in `frame-audio-duplicates.md`.

- Master row controls the server's current default output volume/mute; sliders cap
  user changes at 100%, preserve channel balance, and do not implicitly unmute.
- Sink/server subscriptions follow external volume/mute edits and default output
  changes. A target epoch rejects known-stale commands after A → B → A switches;
  stream and output command namespaces cannot be interchanged.
- Conservative spatializer matching requires a real writable app, current route,
  exact generated node-name stem, shared filter/client identity, virtual/passive
  metadata, one upstream and one output, and matching connection generations.
- The normal list hides matched internal playback only. Internals shows it with an
  explicit label; parent subtitles disclose hidden-stage gain/mute attenuation.
  Missing metadata/shared routes/independent virtual streams remain visible.
- All raw streams remain in fresh exit reset. Master volume and mute remain untouched.

Local x64 Release, ARM64/QEMU and non-PIE ASan/UBSan tests pass. Portable tests include
ambiguous/missing metadata, route/source removal, visibility changes during drag,
master unavailable/default changed during drag, disconnected mute release, loss of
write capability, channel scaling and target identity.
Private Pulse integration tests construct an app → virtual capture → playback
metadata fixture, observe route removal, and verify that a hidden playback stream
is still reset. Two null sinks verify master gain/mute, external edits, default
switching, old-target rejection and preserved master state after app reset. The test
fixture is not a reproduction of Valve's spatial DSP or headset hardware volume.

## Required hardware gates

- [ ] Installer dependency/architecture check on the headset.
- [x] Basic installed native launch reported working by owner.
- [ ] Repeated-launch Show behavior.
- [x] Basic native dashboard/volume controls reported working by owner.
- [ ] Extended readability/input/no-flicker and native panel close testing.
- [ ] Native game plus local Chromium expose independently controllable streams.
- [ ] Stream-only changes do not alter another app or the master output volume.
- [ ] Firefox/Chromium spatializer hiding, Internals controls and attenuation warnings.
- [ ] Master output audibility, mute, native headset-volume synchronization and route changes.
- [ ] Multi-stream browser/Proton metadata and channel balance.
- [ ] Audio/runtime loss, dashboard recreation, sleep/wake, and game switching.
- [ ] Visible/hidden CPU and memory, game/compositor timing versus mixer-off.
- [ ] Quit restores all exposed writable stream gains to 100% without changing mute/master.
- [ ] Update and uninstall leave unrelated files/services untouched.

No device was installed to or changed during initial implementation. The reference
PhoneCast project remains read-only. App grouping, a native PipeWire fallback,
autostart, and persistent app rules are not shipped. Spatializer hiding is not general app grouping.
