#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class TrackDataModel;

class ControlBar final : public juce::Component
{
public:
    explicit ControlBar(TrackDataModel* model = nullptr);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::TextButton inspectorButton { "I" };
    juce::TextButton mixerButton { "X" };
    juce::TextButton playButton { "Play" };
    juce::TextButton stopButton { "Stop" };
    juce::TextButton recordButton { "Record" };
    juce::ToggleButton metronomeButton { "Metronome" };
    juce::Slider bpmSlider;
    juce::Label bpmLabel;
    juce::Label timecodeLabel;
    juce::TextButton pointerTool { "POINTER" };
    juce::TextButton scissorsTool { "SCISSORS" };
    TrackDataModel* trackModel = nullptr;
};
