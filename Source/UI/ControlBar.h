#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Project/ProjectState.h"

class TrackDataModel;
class AudioEngine;

class ControlBar final : public juce::Component, private juce::Timer
{
public:
    explicit ControlBar(TrackDataModel* model = nullptr, AudioEngine* engine = nullptr);

    std::function<void()> onInspectorToggle;
    std::function<void()> onMixerToggle;
    std::function<void()> onPianoRollToggle;
    std::function<void()> onBrowserToggle;
    std::function<void(TrackType)> onCreateTrack;
    std::function<void()> onRecordRequested;

    void setInspectorVisible(bool visible) noexcept;
    void setMixerVisible(bool visible) noexcept;
    void setPianoRollVisible(bool visible) noexcept;
    void setBrowserVisible(bool visible) noexcept;
    void togglePlayback() noexcept;
    void stopPlayback() noexcept;
    void toggleRecording();
    void setRecordActive(bool active) noexcept;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    juce::TextButton inspectorButton { "I" };
    juce::TextButton mixerButton { "X" };
    juce::TextButton pianoRollButton { "P" };
    juce::TextButton browserButton { "B" };
    juce::TextButton addTrackButton { "+" };
    juce::TextButton playButton { "Play" };
    juce::TextButton stopButton { "Stop" };
    juce::TextButton recordButton { "Record" };
    juce::ToggleButton metronomeButton { "Metronome" };
    juce::Slider bpmSlider;
    juce::Label bpmLabel;
    juce::Label timecodeLabel;
    juce::TextButton pointerTool { "POINTER" };
    juce::TextButton scissorsTool { "SCISSORS" };
    juce::TextButton eraserTool { "ERASER" };
    TrackDataModel* trackModel = nullptr;
    AudioEngine* audioEngine = nullptr;
};
