#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Project/ProjectState.h"
#include "AudioPeakMeter.h"
#include "TransportLCD.h"

class TrackDataModel;
class AudioEngine;

class TransportIconButton final : public juce::Button
{
public:
    enum class Icon { goToBeginning, rewind, play, stop, forward, record };

    explicit TransportIconButton(Icon icon);
    void paintButton(juce::Graphics& graphics, bool highlighted, bool down) override;

private:
    Icon icon;
};

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
    std::function<void(bool)> onCountInChanged;
    std::function<void(bool)> onPunchChanged;

    void setInspectorVisible(bool visible) noexcept;
    void setMixerVisible(bool visible) noexcept;
    void setPianoRollVisible(bool visible) noexcept;
    void setBrowserVisible(bool visible) noexcept;
    void togglePlayback() noexcept;
    void stopPlayback() noexcept;
    void goToBeginning() noexcept;
    void rewindOneBar() noexcept;
    void forwardOneBar() noexcept;
    void toggleCycle() noexcept;
    void toggleRecording();
    void setRecordActive(bool active) noexcept;
    void setRecordCountdown(bool active) noexcept;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateLogoPlaybackGlow(bool shouldGlow) noexcept;

    juce::TextButton inspectorButton { "I" };
    juce::TextButton mixerButton { "X" };
    juce::TextButton pianoRollButton { "P" };
    juce::TextButton browserButton { "MEDIA" };
    juce::TextButton addTrackButton { "+ TRACK" };
    TransportIconButton goToBeginningButton { TransportIconButton::Icon::goToBeginning };
    TransportIconButton rewindButton { TransportIconButton::Icon::rewind };
    TransportIconButton forwardButton { TransportIconButton::Icon::forward };
    TransportIconButton playButton { TransportIconButton::Icon::play };
    TransportIconButton stopButton { TransportIconButton::Icon::stop };
    TransportIconButton recordButton { TransportIconButton::Icon::record };
    juce::ToggleButton countInButton { "COUNT" };
    juce::ToggleButton punchButton { "PUNCH" };
    juce::ToggleButton cycleButton { "CYCLE" };
    juce::ToggleButton metronomeButton { "Metronome" };
    juce::Slider bpmSlider;
    juce::Label bpmLabel;
    juce::Image brandLogo;
    juce::Image illuminatedBrandLogo;
    float logoPlaybackGlow = 0.0f;
    TransportLCD transportLCD;
    juce::Label transportTimeLabel;
    AudioPeakMeter masterMeter;
    juce::TextButton pointerTool { "POINTER" };
    juce::TextButton scissorsTool { "SCISSORS" };
    juce::TextButton eraserTool { "ERASER" };
    TrackDataModel* trackModel = nullptr;
    AudioEngine* audioEngine = nullptr;
};
