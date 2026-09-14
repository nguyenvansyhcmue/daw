# Phase 6: Undo/Redo Foundation

`TrackDataModel` now owns a bounded 128-entry domain history. Each entry captures track order, stable tracks/clips and their decoded media ownership, mixer values, tempo/cycle state and FX rack identity/state. UI controls remain direct model clients and do not implement history.

Existing add/remove/reorder track, gain/pan/mute/solo, and add/move/trim/split/delete clip mutations commit a pre-edit memento. Undo and redo restore that domain state and republish immutable clip, FX and render snapshots through the established realtime-safe publication path. Undo history is intentionally cleared after project load and is not serialized.

Tests cover multi-step clip edits, mixer undo/redo, structural track operations, preserved clip media, stable IDs and coherent render snapshots after restoration.
