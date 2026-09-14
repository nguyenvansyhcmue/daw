# Phase 2: realtime snapshot ownership and stable identity

## Current flow before this phase

Control/UI code built a `shared_ptr` clip or FX snapshot, atomically published
it, and the audio callback copied that `shared_ptr`. Replacing the published
snapshot could leave the callback holding the final reference; destruction of
the vector, buffers, or processor ownership could then occur on the audio
thread.

Tracks were identified by their position in `trackStates`, and audio clips
stored that same position in `trackID`. Reordering or removing a track would
therefore change the apparent identity of later tracks.

## Ownership model after this phase

The control thread owns the active immutable clip snapshot and FX snapshots
with `unique_ptr`. It publishes raw pointers only. An audio callback creates
`RealtimeSnapshotRead`, increments a reader count, and captures all raw
pointers once. It performs no reference-counting or destruction. When the
control thread replaces a snapshot, it moves the old owner to a retired list.
It only clears retired lists when the reader count is zero. `MixerPane` calls
that reclaim method on its UI timer, so a final replacement is eventually
reclaimed without waiting for another publication.

The safety invariant is: after the reader count is incremented, a reader either
captures the new published pointer, or the publisher observes the reader before
destroying the old owner. Structural track mutations are rejected while
playback is active, so the audio callback never traverses a relocating track
vector.

## Identity model

`TrackId` and `ClipId` are monotonic integer value types. `TrackState` owns a
`TrackId`; UI order remains the vector index. Audio clips store `TrackId`, and
the engine resolves it to the current temporary processing index. A clip keeps
its `ClipId` when moved or trimmed. On split, the left/original clip retains
its ID and the new right-hand clip receives a new ID. FX rack storage remains
fixed-size, but each rack is mapped to its owning `TrackId`, not track order.

## Scope limits

This does not add project persistence, recording, MIDI, plugins, automation,
or routing. Track structural mutation is intentionally constrained to stopped
transport until a future structural render snapshot is introduced. Clip edits
publish immutable clip snapshots and remain valid during playback.
