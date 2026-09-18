#include "DeviceSettingsPanel.h"

#include "../AudioEngine/AudioEngine.h"

DeviceSettingsPanel::DeviceSettingsPanel(AudioEngine& engine)
    : audioEngine(engine),
      selector(audioEngine.getAudioDeviceManager(), 0, 64, 2, 64, true, true, true, false)
{
    summary.setJustificationType(juce::Justification::centredLeft);
    summary.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.75f));
    addAndMakeVisible(summary);
    addAndMakeVisible(selector);
    startTimerHz(5);
    timerCallback();
}

void DeviceSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced(16);
    summary.setBounds(area.removeFromTop(26));
    selector.setBounds(area);
}

void DeviceSettingsPanel::timerCallback()
{
    const auto diagnostics = audioEngine.getRealtimeDiagnostics();
    summary.setText("Active: " + juce::String(juce::roundToInt(diagnostics.sampleRate)) + " Hz, "
                        + juce::String(diagnostics.bufferSize) + " samples, "
                        + "input latency " + juce::String(diagnostics.inputLatencySamples) + " samples, "
                        + "output latency " + juce::String(diagnostics.outputLatencySamples) + " samples",
                    juce::dontSendNotification);
}
