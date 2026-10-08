# Native Frame duplicate-row investigation

Date: 2026-10-07. Scope: owner-authorized, read-only SSH inspection while Firefox
was playing and both labeled/Unknown mixer rows were visible. No volumes, settings,
packages, processes, or services were changed on the headset.

## Finding

These are **two gain stages of one audio path**, not two browser playback instances:

```text
Firefox application stream
  Pulse sink-input 200 / PipeWire node 175
       ↓
Firefox spatializer input sink
  Pulse sink 202 / PipeWire node 177
       ↓ internal filter-chain processing
Firefox spatializer playback stream (the Unknown row)
  Pulse sink-input 203 / PipeWire node 178
       ↓
Built-in Audio Playback
  Pulse sink 151 / PipeWire node 125
```

Identifiers above belong only to this snapshot and must not be hard-coded.

Evidence:

- Real app stream: `application.name=Firefox`,
  `application.process.binary=firefox-bin`, Flatpak portal app ID
  `org.mozilla.firefox`; connected to sink 202.
- Extra playback stream: no application name or Pulse client; node name
  `Firefox.200.spatialize_filter_chain.playback`, `node.virtual=true`,
  `node.passive=true`, `media.class=Stream/Output/Audio`.
- Its client in the native PipeWire graph is **WirePlumber**, not Firefox.
- Capture sink and playback node share the same `node.link-group` and filter-chain
  description. The sink name ends in `.spatialize_filter_chain.capture`.
- PipeWire graph links confirm Firefox node 175 feeds capture node 177, and playback
  node 178 feeds output node 125, in both channels.
- WirePlumber's status command already classifies this pair under Filters, not apps.

The 0.1.x mixer exposed every sink input as an application row, and fell back
to Unknown when a filter stream had no application metadata. Both sliders affect
Firefox because the same audio passes through both stages. No volume-write test
was performed during this inspection; that audible behavior is owner-reported.

A separate VRLink stream was also present with `media.name=Valve VRLink` but no
application name. It is not the Firefox spatializer and must not be hidden just
because its application metadata is absent.

## Fix implemented in 0.2.0 (headset retest pending)

1. Keep the real application stream as the normal Firefox control.
2. Use sink discovery to match its target capture sink with the internal spatializer
   playback stream, checking filter type/name, shared link-group and virtual/passive
   metadata. Require a real upstream app and a current matching route.
3. Hide only a confidently matched internal spatializer row in the ordinary app list.
   Do not collapse solely by display name, drop all Unknown rows, or hide all virtual
   streams. Unmatched streams remain visible; improve their labels via `media.name`
   or `node.description` (e.g. Valve VRLink).
4. Keep internal streams in backend state and the all-stream exit-reset path. Filtering
   the UI must not remove them from shutdown cleanup.
5. The **Internals** toggle reveals matched spatializer rows. A hidden stage's
   non-100% gain or mute appears in its parent row subtitle so external attenuation
   is not silently ignored.
6. Subscribe to sink/route changes so stream replacement, spatialization disablement,
   missing metadata, and transient discovery ordering safely fall back to visible rows.

Implementation additionally requires exactly one upstream app and one matching
playback node, a writable upstream stream, matching connection generations/client
metadata, and the observed `<upstream node>.<Pulse index>.spatialize_filter_chain`
name stem. Ambiguous/shared pipelines, unknown routes, other virtual streams and
missing metadata remain visible. Filtering affects only the UI, not discovery or
exit cleanup. Test fixtures mirror the observed metadata without hard-coded live IDs;
private Pulse tests exercise discovery, route changes and hidden-stream exit reset.
No updated binary has been installed remotely.

Raw evidence is local and Git-ignored under
`out/diagnostics/frame-audio-20261007-205805/`: Pulse sink inputs/sinks/clients,
server info, PipeWire graph, WirePlumber status, and process names. It may include
private media/process metadata and should not be committed or published wholesale.
Connection credentials remain in the ignored `.env`; the probe validates against
existing OpenSSH known-host keys and never prints authentication values.
