#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioPeakMeter.h"

class TrackDataModel;
class AudioEngine;

class InspectorPane final : public juce::Component, private juce::Timer
{
public:
    explicit InspectorPane(TrackDataModel* model = nullptr, AudioEngine* engine = nullptr);
    void setSelectedTrack(int track) noexcept;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void refreshControls();
    void timerCallback() override;

    TrackDataModel* trackModel = nullptr;
    AudioEngine* audioEngine = nullptr;
    int selectedTrack = 0;
    juce::Label title { {}, "Inspector" };
    juce::Label selectedStripTitle { {}, "SELECTED TRACK" };
    juce::Label masterStripTitle { {}, "STEREO OUT" };
    juce::Label trackName;
    juce::Label eqSlot { {}, "CHANNEL EQ" };
    std::array<juce::TextButton, 4> insertSlots;
    juce::Slider volume;
    juce::Slider pan;
    juce::Slider masterVolume;
    AudioPeakMeter trackMeter;
    AudioPeakMeter masterMeter;
    juce::ToggleButton mute { "Mute" };
    juce::ToggleButton solo { "Solo" };
    juce::ToggleButton arm { "Record Enable" };
};
