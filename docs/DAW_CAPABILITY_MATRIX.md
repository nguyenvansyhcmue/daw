# DAW capability matrix

Status values: IMPLEMENTED, PARTIAL, STUB, MISSING, UNKNOWN, BLOCKED.

| Capability | Status | Evidence / limitation | Next action |
|---|---|---|---|
| Project/session model | PARTIAL | Versioned XML round-trip preserves ordered tracks, stable IDs, clips, mixer and transport state. | Add project metadata and migrations only when needed. |
| Timeline | PARTIAL | Audio clip drawing, trim/split/delete and snap exist in `ArrangeWindow`. | Replace demo `ClipBlock` path and add commands/undo. |
| Transport | PARTIAL | Engine-owned playhead, play/stop and cycle playback are Phase 1 scope. | Add seek, count-in and command routing. |
| Audio playback | PARTIAL | Project load reloads referenced media into immutable cached buffers; no disk streaming. | Add asset streaming before long-project support. |
| Audio device management | PARTIAL | JUCE default device manager; active sample rate is propagated to model. | Device selection/error reporting. |
| Audio recording / disk streaming | MISSING | No capture or disk writer. | Implement after playback core. |
| Track model / audio clips | PARTIAL | Fixed eight-track mixer; stable IDs; immutable render structures; observable ready/missing/decode-failed media states. | Dynamic capacity and non-destructive clip editing. |
| MIDI engine / piano roll | MISSING | `ClipBlock::isMidi` is visual metadata only. | Future milestone. |
| Mixer | PARTIAL | Gain, linear balance pan, mute, solo, inserts, master graph and per-track meters. | Buses, sends, routing and automation. |
| Buses, sends, sidechains | PARTIAL | Stable bus routes support track→master, track→bus and post-fader send→bus→master. | Add bus FX, pre-fader sends, persistence and UI. |
| Plugin hosting / PDC | MISSING | Only internal `AudioEffectProcessor` and Gain Utility exist. | Plugin subsystem after routing. |
| Automation | MISSING | No parameter or automation model. | Add after stable track/routing IDs. |
| Audio editing / fades | PARTIAL | Non-destructive split, trim, delete, gain and fade envelopes render in the production path. | Add duplicate/clipboard and automatic crossfade creation. |
| Tempo map | PARTIAL | In-memory BPM events; playback is sample-position anchored. | Centralize musical-time conversions and tests. |
| Time signatures / metronome | STUB | Numerator only; metronome is UI-only. | Complete signature model and metronome. |
| Persistence / import/export / render | PARTIAL | Versioned project persistence and deterministic offline WAV bounce reuse production mixing. | Add format options and export UI. |
| Metering | PARTIAL | Post-track-processing peak telemetry. | Ballistics, master meter and clipping. |
| Undo/redo / commands | PARTIAL | Bounded domain history covers current track, mixer and core clip edits; history is not persisted. | Extend alongside new edit types. |
| Realtime safety | PARTIAL | Audio callbacks capture immutable clip and structural render snapshots under one reader guard; model-owned control timer reclaims retired owners. | Add allocation instrumentation. |
| Test infrastructure | PARTIAL | Phase 1 adds deterministic offline core tests through CTest. | Expand to integration/render tests. |
