#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

class AudioEngine;

class DeviceSettingsPanel final : public juce::Component,
                                  private juce::Timer
{
public:
    explicit DeviceSettingsPanel(AudioEngine& engine);
    ~DeviceSettingsPanel() override;

    void resized() override;

private:
    void timerCallback() override;
    void localizeAudioDeviceErrors();

    AudioEngine& audioEngine;
    juce::Label summary;
    juce::AudioDeviceSelectorComponent selector;
    juce::String lastSummary;
};
