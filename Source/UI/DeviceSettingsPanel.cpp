#include "DeviceSettingsPanel.h"

#include "../AudioEngine/AudioEngine.h"
#include "AudioDeviceErrorLocalizer.h"

DeviceSettingsPanel::DeviceSettingsPanel(AudioEngine& engine)
    : audioEngine(engine),
      selector(audioEngine.getAudioDeviceManager(), 0, 64, 2, 64, true, true, true, false)
{
    summary.setJustificationType(juce::Justification::centredLeft);
    summary.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.75f));
    addAndMakeVisible(summary);
    addAndMakeVisible(selector);
    startTimerHz(4);
    timerCallback();
}

DeviceSettingsPanel::~DeviceSettingsPanel()
{
    audioEngine.saveAudioDeviceState();
}

void DeviceSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced(16);
    summary.setBounds(area.removeFromTop(26));
    selector.setBounds(area);
}

void DeviceSettingsPanel::timerCallback()
{
    localizeAudioDeviceErrors();

    const auto diagnostics = audioEngine.getRealtimeDiagnostics();
    const auto currentSummary = "Active: " + juce::String(juce::roundToInt(diagnostics.sampleRate)) + " Hz, "
                              + juce::String(diagnostics.bufferSize) + " samples, "
                              + "input latency " + juce::String(diagnostics.inputLatencySamples) + " samples, "
                              + "output latency " + juce::String(diagnostics.outputLatencySamples) + " samples";

    if (currentSummary != lastSummary)
    {
        lastSummary = currentSummary;
        summary.setText(lastSummary, juce::dontSendNotification);
    }
}

void DeviceSettingsPanel::localizeAudioDeviceErrors()
{
    AudioDeviceErrorLocalizer::localizePendingOpenDeviceFailure();
}
