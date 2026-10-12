# Independent review: microphone monitor fades then latches silent

You are reviewing a JUCE C++ DAW on macOS using a Focusrite Scarlett Solo.

## Repro

1. Select the Scarlett as the input/output device at 48 kHz, 64 samples.
2. Click `I` on an audio track; this should software-monitor its Input 1 to outputs 1/2.
3. Speak close to the microphone, then move the person/microphone away from the computer while keeping the mouth close to the mic.
4. In this DAW the output gets quieter and eventually becomes silent. Moving back close does not restore it. The same interface/mic is stable in Logic Pro.

## Required review

Find a concrete code-level cause for a fade/latch/silence in the audio input-monitor path. Do not attribute it to microphone gain, distance, phantom power, cables, or generic DAW advice unless the supplied code proves the application cannot cause it.

Audit for:

- realtime thread safety, snapshot lifetime, atomics, stale input/output channel maps;
- callbacks whose size/device state may change and leave a silent state;
- monitor permissions or destinations that can be cleared asynchronously;
- duplicated monitor routes, feedback protection, mute/solo/Panic interactions;
- incorrect JUCE `AudioIODeviceCallback` assumptions;
- logic that could behave differently from Logic Pro with the same CoreAudio device.

Propose a minimal safe patch, plus a deterministic diagnostic that distinguishes: input callback became silent; monitor state disabled; physical output became silent. Do not recommend adding a noise gate or an AGC.

## Files to inspect in full

- `Source/AudioEngine/AudioEngine.h`
- `Source/AudioEngine/AudioEngine.cpp`
- `Source/Models/TrackDataModel.h`
- `Source/Models/TrackDataModel.cpp`
- `Source/UI/ArrangeWindow.cpp`
- `Source/UI/MainComponent.cpp`
- `Source/Core/WorkspaceTransitionManager.cpp`

## Relevant commands

Run these from the repository root and attach their complete output to the review:

```zsh
git diff -- Source/AudioEngine/AudioEngine.h Source/AudioEngine/AudioEngine.cpp Source/Models/TrackDataModel.h Source/Models/TrackDataModel.cpp Source/UI/ArrangeWindow.cpp Source/UI/MainComponent.cpp Source/Core/WorkspaceTransitionManager.cpp
sed -n '1240,1700p' Source/AudioEngine/AudioEngine.cpp
sed -n '1,90p' Source/Models/TrackDataModel.cpp
sed -n '2110,2185p' Source/Models/TrackDataModel.cpp
sed -n '405,555p' Source/UI/ArrangeWindow.cpp
sed -n '850,890p' Source/UI/MainComponent.cpp
```

The current implementation was modified several times during debugging. Treat the checked-out source, not comments or prior descriptions, as authoritative.
