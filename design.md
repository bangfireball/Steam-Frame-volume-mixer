# Steam Frame Volume Mixer — Design

Status: first implementation complete in code; local Linux x64 / ARM64 builds and isolated tests pass. Owner reports basic launch/UI/volume controls working on Steam Frame. Version 0.2.0 adds approved master output controls and conservative spatializer matching. Broader hardware validation and feature retests remain pending. See `docs/validation.md`.
Research date: 2026-10-07.

## 1. Goal

A small native Steam Frame utility that opens from the dashboard **`+` → program launcher** and lets the user change each application's playback volume while a game keeps running.

Example: keep Chromium music at 100% and lower a game to 35%, or reverse those levels. The master row exposes the audio server's default output volume/mute as the overall listening-level control; individual app sliders set the balance underneath it.

Primary target: SteamOS/Linux ARM64 on the headset, not a Windows-hosted overlay. Working name: **Frame Volume Mixer**.

### MVP

- Discover current playback streams without capturing or processing audio.
- Display recognizable application names, current volume, and mute state.
- Independently change game, Chromium, PhoneCast, and other exposed playback streams.
- Control default output volume/mute in a distinct master row without changing the default device.
- Hide only confidently matched internal Frame spatializers; retain an Internals view and all-stream exit reset.
- Open a laser-interactive OpenVR dashboard panel from a user-installed launcher.
- A second launch opens the existing panel rather than creating another process.
- Reflect changes made by other mixers and tolerate applications starting/stopping.
- Keep rendering/work low when hidden; recover after audio/runtime interruptions.
- Install/uninstall under the ordinary user's home directory, without root.

### Not in the MVP

- Native Quick Settings/Quick Access injection, Steam client patching, or private APIs.
- Audio capture, loopback, effects, equalizers, automatic ducking, crossfading, or routing.
- Microphone controls, output routing/device selection, or controlling all device volumes together.
- Separate Chromium-tab controls unless those tabs expose separate audio streams.
- A PC-side mixer agent, network protocol, or controlling remote PC audio sessions.
- A framework-sized dependency on the PhoneCast application.

## 2. Evidence and feasibility

### Read-only reference project review

Reviewed `C:/Projects/vr_mobile_overlay` without changing it:

- `docs/overlay-launcher-and-gesture-research.md`: public OpenVR dashboard APIs; no documented third-party native Quick Access extension found. Dashboard interactions avoid stealing game controller bindings.
- `docs/frame-overlay-plugins-research.md`: native ARM64 Vulkan overlay patterns, dashboard recovery, single-instance launch, and user-level lifecycle patterns from Frame Perf Overlay / Frame Mic Tuner. Their hardware reports are third-party evidence, not tests performed here.
- `docs/phone-audio.md`: previous read-only device probe found AArch64, PipeWire 1.6.8's PulseAudio-compatible server, `libpulse.so.0`, and a 48 kHz stereo speaker sink. Owner-approved PhoneCast playback coexisted with a standalone VR game. This supports feasibility, but does not prove this mixer's stream-control behavior.
- `packaging/steam-frame/install-phonecast.sh`: user-home payload, `.desktop` launcher, absolute launch path, and 48/128/256 px icons. The installer explicitly directs users to dashboard `+`.

No code was copied from the local reference project. The implementation subsequently vendors the MIT-licensed Vulkan uploader from the external Frame Mic Tuner repository with hardening changes and full attribution; see `THIRD_PARTY.md`.

### External research conclusions

1. PulseAudio's introspection API enumerates **sink inputs** (playback streams) and changes their individual volumes/mute states. It is not limited to the controlling program's own playback.
2. `pipewire-pulse` is a PulseAudio-compatible server, making **libpulse the preferred first backend** rather than a firmware-specific WirePlumber override.
3. PulseAudio recommends subscribing to external changes and leaving volume restoration to the sound server. WirePlumber documents stream volume/mute restoration, but actual Frame policy needs validation.
4. OpenVR supports a dashboard overlay with main and thumbnail handles. Existing native plugin research provides a plausible Vulkan rendering and `+` launch path.
5. Quick Settings injection is not established by a public API. Lack of a discovered API is not proof that no internal mechanism exists; it is sufficient reason not to make injection a shipping dependency.

### Critical limitation: local versus PC-streamed audio

This utility controls streams exposed by the **headset's local audio server**. Native game audio and local Chromium should be separately controllable if exposed as separate streams. A streamed PC game/browser may arrive at Frame as **one already-mixed remote stream**. The headset cannot recover independent PC app volumes from that mix. Separate PC-session controls would require a later companion running on the PC.

Likewise, separate Android sources mixed by PhoneCast appear as PhoneCast audio, not independently controllable phone applications.

## 3. Chosen architecture

```text
Dashboard + / .desktop launcher
        ↓ launch or request Show from resident process
Frame Volume Mixer (one ordinary-user process)
├── Mixer model: streams, display groups, commands, operation status
├── Pulse backend: libpulse discovery, subscriptions, volume/mute writes
├── Dashboard adapter: OpenVR events, panel visibility and recovery
├── Renderer: small 2D panel → persistent Vulkan overlay texture
└── Lifecycle: instance lock, local show IPC, settings and diagnostics
        ↓ control requests only
pipewire-pulse → existing playback streams → existing output devices
```

The mixer is a **control client**, not an audio pipeline. It creates no playback/capture streams, changes no default devices, loads no server modules, and installs no WirePlumber policy.

### Implementation baseline

- C++20 and CMake; native Linux ARM64 release bundle.
- libpulse asynchronous API; prefer runtime loading of `libpulse.so.0`, following the reference project's missing-library handling pattern. Headers are a build dependency; the installed OS supplies the client library/server.
- OpenVR `VRApplication_Overlay` with a unique application/overlay identity and manifest.
- Vulkan `SetOverlayTexture` submission, avoiding repeated dynamic `SetOverlayRaw` updates (reference project reports native flicker).
- Small CPU-rendered 2D dashboard initially; choose/pin font and rendering dependencies during the UI spike, including licenses. A large desktop/web runtime is not the default.
- Portable model and tests contain no Pulse/OpenVR/Vulkan handles.
- Windows is a development host, not an MVP audio-control target. A mock backend/headless UI preview is sufficient there.

## 4. Audio discovery and control

### Connection and enumeration

Connect to the launching user's normal Pulse server; do not hard-code `/run/user/...` or assume SSH and Steam launch environments are identical. Inspect launch-time `XDG_RUNTIME_DIR`, server/library availability, and permissions without logging authentication secrets.

When ready:

1. Install subscription callback and subscribe to sink-input, sink and server changes.
2. Enumerate `pa_context_get_sink_input_info_list`, `pa_context_get_sink_info_list` and `pa_context_get_server_info`. Initial discovery waits for all three. Copy route/filter properties alongside volume metadata; derive default output from the server's current name.
3. Reconcile events arriving during enumeration; a stale list must not resurrect removed streams.
4. Deep-copy callback-owned data before returning from libpulse callbacks.
5. Re-query changed/new streams; remove deleted streams immediately.
6. Rebuild state after reconnect. Distinguish "no playback streams" from "audio server unavailable".

Core record: connection generation, stream incarnation, Pulse sink-input index, client association, copied metadata, channel map/volume, mute, corked state, target sink, and control capability flags.

Pulse sink-input indices are **not PipeWire object IDs** and are not durable app identifiers. Bind queued commands to connection generation plus stream incarnation, cancel them on removal/reconnect, and revalidate before dispatch. Index reuse must not knowingly apply a stale drag to another application; an irreducible server-side remove/reuse race remains an integration-test concern.

### Writes

- Volume: `pa_context_set_sink_input_volume`.
- Mute: `pa_context_set_sink_input_mute`.
- Master volume/mute: `pa_context_set_sink_volume_by_index` / `pa_context_set_sink_mute_by_index`, only on the currently resolved default output. Command kinds and target tokens are distinct from app commands. A default-route epoch rejects queued controls after output changes, including A → B → A; missing/default-deleted outputs disable the row. Master changes never move streams or select a default device.
- Respect `has_volume` / `volume_writable` and surface unsupported controls.
- Keep I/O on an audio event-loop worker; send immutable snapshots and bounded command queues to/from the UI.
- Coalesce drag commands to the latest value per target, initially at most 20 writes/second, and commit the final release value.
- Unref operations and bound outstanding work. Timeouts/reconnects cancel obsolete work rather than retrying old writes on new streams.
- Mark changes pending until acknowledged and read back. On failure show actual state and a small error rather than false success.
- Do not continuously reassert a desired volume against another mixer. External changes are authoritative once the user's current drag completes.

### Volume semantics

MVP slider: 0–100%, where 100% is `PA_VOLUME_NORM` (0 dB), **not amplification** and not a percentage of master volume. Label it consistently with Pulse volume convention, not as a claim of perceptually linear loudness.

Preserve per-channel balance by scaling the existing channel-volume vector using its maximum as the displayed level. If all channels are zero, a positive user adjustment sets all channels to the requested level. Clamp safely. Show externally set levels above 100% as over-range without silently changing them; boosting is not offered initially.

Mute remains separate from volume: dragging does not silently unmute, and unmuting retains the prior stream volume. Muted status must be clearly visible.

## 5. Applications versus streams

One program may create several sink inputs; a browser may recreate streams on every track. A sink input is not necessarily one program, tab, or process.

- Build labels from metadata such as `application.name`, `application.id`, `application.process.binary`, client properties, and `media.name`.
- Prefer a trustworthy stable application ID for presentation groups. PID/name alone is not a durable identity; unrelated unknown streams must not be merged just because their labels match.
- Do not assume Wine/Proton streams expose useful Steam app IDs until probed.
- Use a generic icon when metadata cannot safely resolve an installed icon; treat metadata as untrusted text, never as shell commands or arbitrary paths.
- Show dormant/corked streams, labeled inactive; do not claim a program has no audio solely because it is paused.

**First backend milestone:** one row per stream with app name and disambiguating subtitle. This proves independent control before introducing identity heuristics.

**MVP headset UX:** confidently identified multi-stream apps may have a collapsible group with individual rows. A group control explicitly changes the currently listed member streams, shows mixed state when levels differ, and reports partial failures. It is not transactional. Do not automatically assign its value to future streams in the MVP; server restoration governs those. Uncertain identity remains separate rows.

No separate preferences database for per-app volumes initially. Save only UI preferences. Per owner request, version 0.1.1 resets all currently exposed writable playback streams to 100% (0 dB) on normal app exit, preserving channel balance. It does not restore guessed pre-launch values or change mute/master volume. Hiding the dashboard while the utility remains running does not reset volumes.

### Conservative spatializer matching — 0.2.0

The read-only Firefox investigation found `app → virtual capture sink → spatializer playback → default output`; the extra playback row lacks an application name. The normal UI hides that row only with an identified writable upstream stream, an exact current route to the capture sink, matching filter link-group/client metadata, virtual/passive flags, matching connection generations and the observed generated name stem. Require one upstream and one matching playback, not merely similar labels. Missing or ambiguous metadata remains visible.

All raw streams remain in the backend snapshot and fresh shutdown enumeration. An Internals toggle restores the hidden controls with explicit spatializer labels. Parent subtitles warn when a hidden stage has non-100% gain or is muted. General multi-stream app grouping is still deferred; independent tabs/streams remain independently controllable. Media/node metadata supplies useful labels for unidentified non-filter streams such as VRLink.

## 6. Dashboard UX and launch behavior

```text
Frame Volume Mixer                    Connected
Master — Default output        [ ]     ━━━●━━  50%
Application / stream           Mute    Volume
Chromium — Music               [ ]     ━━━━━● 100%
Game                           [ ]     ━●━━━━  35%
PhoneCast                      [x]     ━━━●━━  65%

[Previous] [Next] [Internals] [Reconnect] [Quit]
Quit resets app gains; master stays unchanged
```

Large laser targets, scrollable rows, readable contrast, stable row ordering during interaction, percentage labels, and press/drag/release handling. No pointer capture can survive dashboard close, focus loss, stream removal, or reconnection. Input maps OpenVR mouse coordinates into panel coordinates, including the Y-axis origin difference.

- Create dashboard main and thumbnail handles; set mouse input and scale explicitly.
- First manual launch creates the dashboard and requests `ShowDashboard` once ready.
- A second launch sends a `Show` request over a user-private local socket to the resident process. Use a lock under the user runtime directory, restricted permissions, same-user peer checks, and bounded IPC failure handling. Do not signal an unverified stale PID.
- Dismissing the dashboard or switching tabs hides the panel, not the utility. Explicit Quit or the native panel close button terminates it and runs the exit volume reset. Native close-button behavior still needs physical follow-up.
- Render only when visible and dirty; no continuous animation/audio meters in the MVP.
- Detect missing/replaced overlay handles and recreate the panel/thumbnail with rate-limited retries. Do not destroy handles owned by another process.
- Follow compositor-safe texture teardown; validate lifetime/synchronization before freeing Vulkan resources.

The launcher and resident dashboard tab are different surfaces: installing a `.desktop` file supplies the `+` entry; the running process supplies the dashboard tab. Manifest registration alone does not prove either behavior on Frame.

## 7. Installation and lifecycle

Planned user-owned locations:

- `~/.local/opt/frame-volume-mixer/`: binary, launcher, manifest, icons/assets.
- `${XDG_DATA_HOME:-~/.local/share}/applications/frame-volume-mixer.desktop`.
- hicolor icon sizes 48, 128, and 256 px.
- `${XDG_CONFIG_HOME:-~/.config}/frame-volume-mixer/`: versioned UI settings.
- User runtime directory: lock/socket; no publicly writable control endpoint.

Use correctly quoted absolute `Exec` paths; do not depend on Steam's `PATH`. Package scripts validate payloads and permissions and supply a clean uninstall. Never change the reference PhoneCast install or identity.

Start with manual launch. Optional autostart is a later opt-in experiment: compare OpenVR autolaunch with a user service coupled to the actual Frame SteamVR lifecycle. Validate the real service name before assuming `steamvr.service`. Do not enable two competing supervisors. Intentional Quit must stay quit; crashes may be restarted only under an explicitly enabled policy.

No root, OS writable-root changes, audio service restarts, Steam UI modifications, or updates during gameplay. Treat SteamOS updates and sleep/wake as compatibility test cases.

## 8. Milestones and acceptance gates

### M0 — Read-only hardware feasibility probe

With explicit device-access details/authorization, record OS/runtime versions, launch environment, and available streams using read-only `pactl info`, `pactl list sink-inputs` and, if needed, `wpctl status` / `pw-dump`. Redact unnecessary process/media details.

Play local Chromium music beside a native game. Confirm separate controllable streams, useful metadata, and whether native PipeWire-only streams appear through Pulse introspection. Do not change volume, install software, or restart anything during this read-only probe.

Gate: independent game/browser streams are visible; otherwise document the limitation and evaluate a native PipeWire control adapter only if required.

### M1 — Backend + headless tests

Implement discovery, subscriptions, volume/mute operations, generation-safe lifecycle, and mock backend. Test against a private Pulse/PipeWire environment with a null sink, not the real user's sound server.

Gate: two synthetic streams change independently, external edits synchronize, remove/recreate/reconnect cannot replay obsolete commands, and queues/retries stay bounded.

### M2 — Native dashboard prototype

Build ARM64, render a small mock mixer, install a user launcher, verify `+`, dashboard tab, pointer slider/mute, second-launch Show, and clean teardown.

Gate: works over a running native game without stealing controls, flicker, duplicate instances, or a stuck pointer. Compilation alone is not acceptance.

### M3 — End-to-end MVP

Connect real discovery/control to the dashboard. Add conservative grouping and clear unsupported/unavailable states.

Gate: owner can lower a native game while Chromium music stays unchanged, then reverse the balance; master volume is unchanged; pause/new track/app restart/external edits behave honestly.

### M4 — Lifecycle and release hardening

Test runtime/audio-server loss, dashboard recreation, sleep/wake, application switches, crash/relaunch, uninstall, and optional autostart. Measure hidden/visible CPU, memory, and game/compositor frame timing against a mixer-off baseline. Set numeric budgets from measurements rather than claiming untested performance.

## 9. Test matrix and risks

Automated: stream snapshot/event ordering; metadata fallback; channel scaling/zero/over-range; mute independence; unsupported capabilities; grouping/partial failure; queue coalescing; removal/index reuse; reconnect; external-write conflict; IPC lock/socket recovery; pointer cancellation; settings parsing/atomic writes; installer paths and uninstall scope.

Physical: local Chromium + native game + PhoneCast; multiple browser streams; Proton/native games; PC streaming (aggregate limitation visible); muted/corked streams; volume restoration after stream recreation; different outputs; both launch orders; repeated `+` launch; dashboard/system close; sleep/wake; SteamVR restart; audio-server interruption in a safe non-game session; uninstall.

Principal risks:

- Headset exposes an aggregate instead of per-app streams: cannot separate audio already mixed elsewhere.
- Native PipeWire streams are not fully exposed/control-writable via Pulse: probe before selecting a second backend.
- App identity varies or streams recreate: conservative grouping and no app-specific automatic enforcement.
- Server restoration differs on Frame: document observed behavior; do not promise persistent app rules yet.
- Session/container environment blocks the socket or runtime: validate launch from `+`, not just SSH.
- Native overlay/runtime updates invalidate assumptions: documented APIs, recovery, and recorded versioned hardware evidence.
- Flat-volume policy could affect sink behavior on a different server: verify the target and confirm no master-volume change during physical acceptance.

## 10. Sources

Primary audio documentation retrieved during this design session:

- PulseAudio server query/control: https://www.freedesktop.org/software/pulseaudio/doxygen/introspect.html
- PulseAudio subscriptions: https://www.freedesktop.org/software/pulseaudio/doxygen/subscribe.html
- PipeWire Pulse compatibility: https://docs.pipewire.org/page_man_pipewire-pulse_1.html
- WirePlumber stream restoration: https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/stream.html

OpenVR primary references identified in the read-only overlay research:

- https://github.com/ValveSoftware/openvr/wiki/IVROverlay::CreateDashboardOverlay
- https://github.com/ValveSoftware/openvr/wiki/IVROverlay::ShowDashboard
- https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h

Native reference implementations (not independently hardware-tested here):

- https://github.com/sasaken1102r/frame-mic-tuner — README fetched in this session; existing overlay research reviewed commit `d1e8c830b00928ab94a44e1d5323613af95263c6`.
- https://github.com/sasaken1102r/frame-perf-overlay — existing overlay research reviewed commit `8baaf7e753ebd449c035c1d11a9830eba04d0201`.

## Initial decision

Build a **native dashboard-only, user-installed, libpulse control utility** with Vulkan texture submission and a single-instance `+` launcher. Keep Quick Settings injection out of the MVP. Independently visible native game/browser streams remain the first hardware acceptance gate.

## Implementation baseline — 0.1.0; exit policy updated in 0.1.1; master/filter UI in 0.2.0

- Implemented native dashboard, Cairo 2D rendering, Vulkan textures, Pulse discovery/subscriptions, stream-only volume/mute writes, token-safe command coalescing, single-instance private IPC, user installer/uninstaller, and ARM64 packaging.
- Uses one row per real playback stream with conservative matched-spatializer hiding in 0.2.0. General application grouping and persistent app rules remain deferred. No settings file is needed yet.
- Version 0.2.0's master row follows the default sink and external changes, using bounded coalescing, readback and target epochs. Master volume/mute are intentionally unchanged by app shutdown cleanup; maximum user-set gain is 100%.
- Links the OS's libpulse dynamically at load time rather than runtime `dlopen`. This reduces symbol-loader plumbing but makes the library a required startup dependency. The installer checks architecture/library loading with `--version`; missing-library installations fail clearly rather than providing an in-dashboard library-unavailable fallback. No audio daemon is bundled or auto-spawned.
- Cairo uses system fonts; no fonts or audio/runtime libraries are redistributed. OpenVR client code is pinned and statically linked for ARM64.
- OpenVR manifest registration is session-temporary, with explicit process identification. Launcher discovery remains via the installed `.desktop` file; no persistent OpenVR autostart registration is made.
- Manual startup only. Closing the dashboard releases pointer interaction and hides the UI; confirmed Quit ends the process. In 0.1.1 an explicit SteamVR quit request is honored by exiting and running the reset; missing runtime/overlay recovery without a quit request remains bounded and never starts SteamVR.
- Automated private null-sink tests exercise independent control, mute preservation, external edits, removal/recreation, stale token rejection, reconnect, and final drag-value coalescing. These tests do not prove native Frame stream visibility, physical volume behavior, or compositor correctness.
- Version 0.1.1 adds opt-in interactive-backend cleanup: drop queued drags, freshly enumerate the audio server, reset writable stream gains to 100%, and wait up to two seconds for completion before disconnect. Ordinary read-only backend use does not opt in. This is best-effort if the audio server is unavailable; crashes, SIGKILL, or power loss cannot run cleanup. Tests cover all-stream reset (including externally lowered streams), queued/in-flight commands at exit, unchanged mute/master, read-only preservation, and unavailable-server bounds.
