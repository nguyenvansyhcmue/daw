# Phase 1: Core playback and mixer

## Current state

The application has a JUCE audio callback, cached audio clips, per-track mixer
state, an `AudioGraph`, and transport/mixer UI controls. Before this phase the
controls were not connected to the model, the graph was not executed, and the
audio callback measured each track from the master output.

## Gap and root cause

Playback state existed independently in `AudioEngine` and `TrackDataModel`,
while the callback only consulted the latter. Mixer controls did not write the
track atomics. Cycle data was only painted in the ruler. The master graph had
no place in the render path.

## Design

`TrackDataModel` owns transport and mixer control state. `AudioEngine` is the
only component that advances the playhead and reads that state in the audio
callback. The callback renders clips into preallocated per-track buffers,
processes inserts, applies track gain/pan/mute/solo, measures each processed
track, mixes to one preallocated master buffer, then executes `AudioGraph`
exactly once on that master buffer.

Cycle segmentation is isolated in `TransportUtils` so a block crossing the
cycle end is rendered in consecutive source ranges without an allocation.

## Acceptance criteria

- Debug and Release builds succeed.
- Offline tests cover gain, mute, pan, solo, master gain, cycle boundary, and
  model sample-rate state.
- The callback has no allocations, file I/O, GUI calls, or blocking locks
  introduced by this phase.
- UI transport and mixer controls write the model/engine state; UI playhead
  only reads the engine-owned model position.
