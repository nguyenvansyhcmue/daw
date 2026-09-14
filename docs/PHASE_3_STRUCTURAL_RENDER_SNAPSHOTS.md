# Phase 3: structural edits during playback

## Root cause

The Phase 2 audio callback still read `trackStates` through `getTrack()` and
resolved clip `TrackId` values by scanning that mutable vector. Adding,
removing, or reordering a track could relocate the vector while the callback
was reading it, so Phase 2 rejected structural edits during playback.

## Render snapshot flow

Control thread mutates domain `trackStates` -> builds immutable
`RenderStructureSnapshot` -> publishes raw pointer -> audio callback captures
it once with the existing `RealtimeSnapshotRead` guard.

The structural snapshot contains only render order, `TrackId`, gain, pan,
mute, solo, and the current FX snapshot pointer. The callback combines it with
the immutable clip snapshot, resolves clip `TrackId` locally in the render
snapshot, and never accesses `trackStates` or performs a model lookup.

The current callback may complete with its old snapshot. A later callback
captures the published new structural order.

## Reclaim ownership

Clip, FX, and structural snapshots use the same reader-count/reclamation
mechanism introduced in Phase 2. `TrackDataModel` now owns periodic control
thread reclamation through its own timer; reclamation no longer depends on
`MixerPane`. FX snapshots are retained until the replacement structural
snapshot is published, so an active structural snapshot cannot contain a
reclaimed FX pointer.

## Scope limits

The existing fixed limit of eight tracks remains: it is imposed by the fixed
eight track buffers, meters, FX rack storage, and mixer UI. This phase makes
structural changes safe within that existing bound; it does not expand it.
