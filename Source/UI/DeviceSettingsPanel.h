#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "../Models/TrackDataModel.h"

class AudioEngine;

class DeviceSettingsPanel final : public juce::Component,
                                  private juce::Timer
{
public:
    DeviceSettingsPanel(AudioEngine& engine, TrackDataModel& model);
    ~DeviceSettingsPanel() override;

    void resized() override;

private:
    void timerCallback() override;
    void localizeAudioDeviceErrors();
    void refreshInputRouting();
    void applyTrackInputConfiguration(size_t trackIndex);
    void applyLiveOptimizedBuffer();
    void startLiveCheck();
    void updateLiveCheck();
    void refreshMidiOutputDevices();
    juce::String makeInputRoutingKey() const;

    AudioEngine& audioEngine;
    TrackDataModel& trackModel;
    juce::Label summary;
    juce::Label liveReadiness;
    juce::TextButton liveOptimizeButton { "LIVE OPTIMIZE" };
    juce::Label liveCheckResult;
    juce::TextButton liveCheckButton { "RUN LIVE CHECK" };
    juce::Label autoKeyStatus;
    juce::TabbedComponent settingsTabs { juce::TabbedButtonBar::TabsAtTop };
    juce::Component devicePage;
    juce::Component routingPage;
    juce::AudioDeviceSelectorComponent selector;
    juce::Label midiOutputLabel { {}, "MIDI OUTPUT" };
    juce::ComboBox midiOutputSelector;
    juce::Label routingHeading { {}, "TRACK INPUT ROUTING" };
    juce::Label routingHint;
    juce::Viewport routingViewport;
    juce::Component routingContent;
    std::array<juce::Label, TrackDataModel::maxTracks> trackInputLabels;
    std::array<juce::ComboBox, TrackDataModel::maxTracks> trackInputSelectors;
    std::array<juce::ComboBox, TrackDataModel::maxTracks> trackInputFormats;
    std::array<juce::ComboBox, TrackDataModel::maxTracks> trackMonitorModes;
    juce::String lastSummary;
    juce::String inputRoutingKey;
    juce::String midiOutputDevicesKey;
    struct LiveCheckRun
    {
        bool active = false;
        double startedAtMilliseconds = 0.0;
        uint64_t overloadBaseline = 0;
        float peakCallbackLoad = 0.0f;
    } liveCheck;
};
