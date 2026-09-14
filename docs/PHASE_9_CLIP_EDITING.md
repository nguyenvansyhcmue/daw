# Phase 9: Core Clip Editing

Audio clips now own non-destructive gain, fade-in and fade-out metadata. The render path applies each clip's gain envelope to source samples before summing, so overlapping clips retain independent amplitudes and can form a manual crossfade using opposing fades.

Gain and fade edits publish immutable clip snapshots and participate in the Phase 6 undo history. Project format v2 persists the fields; the loader continues to accept v1 projects with unity gain and zero fades.

DSP tests verify gain/fade sample amplitudes and undo restoration. Duplicate, clipboard workflows and automatic crossfade creation are not yet implemented.
