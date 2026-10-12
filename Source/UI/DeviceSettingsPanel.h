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
    void refreshStreamOutputDevices();
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
    juce::Component monitoringPage;
    juce::Component routingPage;
    juce::AudioDeviceSelectorComponent selector;
    juce::Label midiOutputLabel { {}, "MIDI OUTPUT" };
    juce::ComboBox midiOutputSelector;
    juce::Label streamOutputLabel { {}, "STREAM OUTPUT" };
    juce::ComboBox streamOutputSelector;
    juce::Label streamOutputStatus;
    juce::Label monitoringHeading { {}, "MONITORING & LATENCY" };
    juce::Label monitoringHint;
    juce::ToggleButton softwareMonitoring { "Software monitoring" };
    juce::ToggleButton focusedInputMonitoring { "Monitor only the focused input track" };
    juce::ToggleButton lowLatencyMonitoring { "Low Latency Monitoring" };
    juce::ComboBox pluginDelayCompensation;
    juce::Label pdcLabel { {}, "Plug-in Delay Compensation" };
    juce::Label pdcHint;
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
    juce::String streamOutputDevicesKey;
    juce::String audioDeviceListKey;
    bool audioDeviceListInitialised = false;
    struct LiveCheckRun
    {
        bool active = false;
        double startedAtMilliseconds = 0.0;
        uint64_t overloadBaseline = 0;
        float peakCallbackLoad = 0.0f;
    } liveCheck;
};
