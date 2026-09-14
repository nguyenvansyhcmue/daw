# Phase 4: Project Persistence

`ProjectSerializer` writes XML project files with `formatVersion="1"`. The schema contains an ordered `Tracks` collection, each track's stable ID and mixer values, nested clips with stable IDs, source paths and sample timing, plus the tempo map, BPM, time-signature numerator and cycle range.

`ProjectState` is a plain persistent-data boundary. It deliberately excludes cached audio buffers, effect/render snapshots, FX pointer ownership, peak meters, audio-device state, transport playback state and UI objects. Existing internal FX processors have no user-configurable state in this repository, so no FX state is serialized.

Load follows `parse -> validate -> candidate ProjectState -> applyProjectState`. Validation rejects malformed XML, an unsupported format version, duplicate/invalid IDs, invalid clip ownership/timing, invalid mixer/transport values, an empty or unordered tempo map, and projects above the current eight-track runtime limit. Failed parsing and validation do not mutate the active model.

On success the model rebuilds clips and fresh empty FX/render snapshots, publishes them through the existing realtime snapshot mechanism, stops transport and resets the playhead. Track and clip allocators resume at one greater than the largest restored ID, preventing collisions.

Media is represented only by the JUCE filesystem path already stored by `AudioClipState`. A missing source is preserved, does not make loading fail, and is returned through the optional `missingMediaReferences` output. The clip has no cached buffer after load, so it is safely silent until a future media-loading phase supplies one.

Save/load are control-thread operations. `ProjectSerializer` is not referenced by `AudioEngine`, and no file I/O or XML work runs in its callback.
