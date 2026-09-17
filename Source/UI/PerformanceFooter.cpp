#include "PerformanceFooter.h"

#include "../AudioEngine/AudioEngine.h"

PerformanceFooter::PerformanceFooter(AudioEngine* engine) : audioEngine(engine)
{
    masterLabel.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    masterLabel.setJustificationType(juce::Justification::centredLeft);
    masterLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.68f));
    addAndMakeVisible(masterLabel);

    masterGain.setRange(0.0, 2.0, 0.01);
    masterGain.setValue(audioEngine != nullptr ? audioEngine->getMasterGain() : 1.0,
                        juce::dontSendNotification);
    masterGain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
    masterGain.onValueChange = [this]
    {
        if (audioEngine != nullptr)
            audioEngine->setMasterGain(static_cast<float>(masterGain.getValue()));
    };
    addAndMakeVisible(masterGain);

    bounceButton.onClick = [this] { if (onBounceRequested != nullptr) onBounceRequested(); };
    addAndMakeVisible(bounceButton);
    automationVisible.setClickingTogglesState(true);
    addAndMakeVisible(automationVisible);
    cpuLabel.setJustificationType(juce::Justification::centredRight);
    cpuLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
    addAndMakeVisible(cpuLabel);
    addAndMakeVisible(cpuMeter);
    startTimerHz(15);
}

void PerformanceFooter::timerCallback()
{
    if (audioEngine != nullptr)
        masterGain.setValue(audioEngine->getMasterGain(), juce::dontSendNotification);
    cpuLoad = audioEngine != nullptr ? juce::jlimit(0.0, 1.0, audioEngine->getAudioDeviceManager().getCpuUsage()) : 0.0;
    cpuLabel.setText("CPU " + juce::String(juce::roundToInt(cpuLoad * 100.0)) + "%", juce::dontSendNotification);
    repaint(cpuMeter.getBounds());
}

void PerformanceFooter::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1c1c1e));
    g.setColour(juce::Colours::white.withAlpha(0.13f));
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
}

void PerformanceFooter::resized()
{
    auto area = getLocalBounds().reduced(8, 5);
    masterLabel.setBounds(area.removeFromLeft(82));
    masterGain.setBounds(area.removeFromLeft(180).reduced(0, 3));
    bounceButton.setBounds(area.removeFromLeft(76).reduced(3, 0));
    automationVisible.setBounds(area.removeFromRight(130));
    cpuMeter.setBounds(area.removeFromRight(92).reduced(3, 5));
    cpuLabel.setBounds(area);
}
