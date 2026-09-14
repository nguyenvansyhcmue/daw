# Phase 8: Offline Bounce

`AudioEngine::renderOfflineWav` renders WAV without an audio device by driving the existing device-callback production mix path into a local buffer and JUCE WAV writer. It therefore preserves timeline placement, track gain/pan/mute/solo, inserts and master graph processing without a second mixer implementation.

Render is control-thread-only, refuses while recording, defaults to the project range and can explicitly use the cycle range. It temporarily sets sample rate/playhead/play state and restores all transport/cycle state after success or writer failure.

The deterministic test verifies no-device output, rendered length and expected track/master gain amplitude. AIFF is not exposed yet; WAV is the supported Phase 8 export format.
