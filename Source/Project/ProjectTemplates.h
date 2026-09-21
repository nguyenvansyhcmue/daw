#pragma once

#include "ProjectState.h"

enum class ProjectTemplate : uint8_t
{
    empty,
    audioRecording,
    midiProduction
};

enum class LiveSessionMode : uint8_t { audio, midi, audioAndMidi };
enum class MidiSessionMode : uint8_t { virtualInstruments, backingTrack, instrumentsAndBackingTrack };

struct LiveSetupConfig
{
    LiveSessionMode sessionMode = LiveSessionMode::audio;
    MidiSessionMode midiMode = MidiSessionMode::virtualInstruments;
    int vocalCount = 1;
    int guitarCount = 0;
    int bassCount = 0;
    int keyboardCount = 0;
    int drumCount = 0;
    int backingTrackCount = 0;
    juce::StringArray vocalNames;
    juce::StringArray guitarNames;
    juce::StringArray bassNames;
    juce::StringArray keyboardNames;
    juce::StringArray drumNames;
    juce::StringArray backingTrackNames;
};

class ProjectTemplates final
{
public:
    [[nodiscard]] static ProjectState create(ProjectTemplate projectTemplate);
    [[nodiscard]] static ProjectState createLiveSetup(const LiveSetupConfig& setup);
    [[nodiscard]] static juce::String getName(ProjectTemplate projectTemplate);
    [[nodiscard]] static juce::String getDescription(ProjectTemplate projectTemplate);
};
