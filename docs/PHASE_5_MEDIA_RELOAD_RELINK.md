# Phase 5: Media Reload and Relink

`MediaReloadService` owns control-thread decoding through JUCE's registered basic formats. `ProjectSerializer::load` applies validated persistent state first, then reloads every referenced clip outside the audio callback and publishes each completed resource through `TrackDataModel`'s existing immutable clip snapshot path.

Decoded media is a shared `AudioBuffer<float>` resource. Clips referring to the same canonical source path reuse one decoded buffer during a reload. `AudioMediaStatus` makes `Ready`, `Missing`, `DecodeFailed` and `Unloaded` observable from the model. Missing or failed media remains a valid silent clip with its stable identity and timing intact.

`relinkClip` decodes and validates a replacement file before changing the clip. A failed relink leaves the existing source, buffer and status unchanged. The engine now bounds source reads against decoded-buffer length, protecting playback if a relinked source is shorter than stored clip timing.

Tests cover valid project reload and playback, shared resources, project-to-project replacement, missing media, failed relink and successful relink. File decoding and allocation remain outside the device callback; publication uses the pre-existing deferred-reclamation snapshot mechanism.
