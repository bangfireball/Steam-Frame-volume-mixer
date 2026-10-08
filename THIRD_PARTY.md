# Third-party dependencies

## Vendored Vulkan uploader

`third_party/frame-mic-tuner/vk_texture.{h,cpp}` is derived from
[sasaken1102r/frame-mic-tuner](https://github.com/sasaken1102r/frame-mic-tuner),
commit `d1e8c830b00928ab94a44e1d5323613af95263c6` (2026).

License: MIT; full notice is in `third_party/frame-mic-tuner/LICENSE` and included
in release bundles under `licenses/frame-mic-tuner/LICENSE`.

Local changes: mixer application identity; require all OpenVR-requested Vulkan
extensions; no arbitrary fallback GPU; validate Vulkan resource/bind/map/command
results and memory types. Original source comments are retained. Hardware behavior
is not independently verified here.

## OpenVR

Valve OpenVR SDK 2.15.6 is fetched at pinned commit
`0924064316de3effbcd1acf1e309182a2deb1c05`. The client is statically linked for the
target architecture. Its BSD-style license is bundled in `licenses/openvr/LICENSE`.
The installed SteamVR runtime is supplied by the headset, not redistributed here.

## System libraries

PulseAudio client library (libpulse), Cairo, Vulkan loader, and their transitively
loaded libraries are dynamically linked from the operating system, not bundled.
Cairo uses the system sans-serif font/fontconfig; no font is redistributed.
Python's standard library generates the original launcher icons at development
time; Python is not an installation/runtime dependency.

The existing `C:/Projects/vr_mobile_overlay` project was inspected read-only as
architecture/build/packaging research. No code from that project was copied.
