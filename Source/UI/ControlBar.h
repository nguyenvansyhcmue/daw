#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Project/ProjectState.h"
#include "AudioPeakMeter.h"
#include "TransportLCD.h"
#include "WorkspaceMode.h"

class TrackDataModel;
class AudioEngine;

// The three workspace selectors are deliberately drawn as console keys rather
// than stock text buttons.  They are the visual entry point into the DAW, so
// the icon and label have to retain their hierarchy at every window size.
class WorkspaceModeButton final : public juce::Button
{
public:
    enum class Mode { studio, stage, stream };

    WorkspaceModeButton(Mode mode, juce::String label);
    void paintButton(juce::Graphics& graphics, bool highlighted, bool down) override;

private:
    Mode mode;
};

class TransportIconButton final : public juce::Button
{
public:
    enum class Icon { goToBeginning, rewind, play, stop, forward, record, metronome, countIn };

    explicit TransportIconButton(Icon icon);
    void setPulsePhase(float phase) noexcept;
    void paintButton(juce::Graphics& graphics, bool highlighted, bool down) override;

private:
    Icon icon;
    float pulsePhase = 0.0f;
};

// Auto Key uses the same recessed hardware-key language as the workspace
// selectors, while keeping its own key-detection icon legible at compact sizes.
class AutoKeyButton final : public juce::Button
{
public:
    AutoKeyButton();
    void paintButton(juce::Graphics& graphics, bool highlighted, bool down) override;
};

class DigitalTimeReadout final : public juce::Component
{
public:
    void setText(juce::String newText, juce::NotificationType notification = juce::sendNotification);
    void paint(juce::Graphics& graphics) override;

private:
    juce::String text { "00:00:00.00" };
};

class ControlBar final : public juce::Component, private juce::Timer
{
public:
    explicit ControlBar(TrackDataModel* model = nullptr, AudioEngine* engine = nullptr);

    std::function<void()> onInspectorToggle;
    std::function<void()> onMixerToggle;
    std::function<void()> onPianoRollToggle;
    std::function<void()> onBrowserToggle;
    std::function<void()> onAutoKeyRequested;
    std::function<void(TrackType)> onCreateTrack;
    std::function<void()> onRecordRequested;
    std::function<void(WorkspaceMode)> onWorkspaceModeChanged;
    std::function<void(bool)> onCountInChanged;
    std::function<void(int)> onCountInBarsChanged;
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
    void setWorkspaceMode(WorkspaceMode mode) noexcept;
    WorkspaceMode getWorkspaceMode() const noexcept { return workspaceMode; }

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
    AutoKeyButton autoKeyButton;
    TransportIconButton goToBeginningButton { TransportIconButton::Icon::goToBeginning };
    TransportIconButton rewindButton { TransportIconButton::Icon::rewind };
    TransportIconButton forwardButton { TransportIconButton::Icon::forward };
    TransportIconButton playButton { TransportIconButton::Icon::play };
    TransportIconButton stopButton { TransportIconButton::Icon::stop };
    TransportIconButton recordButton { TransportIconButton::Icon::record };
    TransportIconButton countInButton { TransportIconButton::Icon::countIn };
    juce::ToggleButton punchButton { "PUNCH" };
    juce::ToggleButton cycleButton { "CYCLE" };
    TransportIconButton metronomeButton { TransportIconButton::Icon::metronome };
    WorkspaceModeButton studioModeButton { WorkspaceModeButton::Mode::studio, "STUDIO" };
    WorkspaceModeButton stageModeButton { WorkspaceModeButton::Mode::stage, "STAGE" };
    WorkspaceModeButton streamModeButton { WorkspaceModeButton::Mode::stream, "STREAM" };
    juce::TextButton liveModeButton { "LIVE" };
    juce::ComboBox countInBarsBox;
    juce::Slider bpmSlider;
    juce::Label bpmLabel;
    juce::Image brandLogo;
    juce::Image illuminatedBrandLogo;
    juce::Image referenceConsoleStrip;
    juce::Rectangle<float> referenceConsoleBounds;
    float logoPlaybackGlow = 0.0f;
    TransportLCD transportLCD;
    DigitalTimeReadout transportTimeLabel;
    AudioPeakMeter masterMeter;
    juce::TextButton pointerTool { "POINTER" };
    juce::TextButton scissorsTool { "SCISSORS" };
    juce::TextButton eraserTool { "ERASER" };
    TrackDataModel* trackModel = nullptr;
    AudioEngine* audioEngine = nullptr;
    WorkspaceMode workspaceMode = WorkspaceMode::studio;
};
