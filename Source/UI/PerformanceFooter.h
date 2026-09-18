#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class AudioEngine;

class PerformanceFooter final : public juce::Component, private juce::Timer
{
public:
    explicit PerformanceFooter(AudioEngine* engine = nullptr);

    std::function<void()> onBounceRequested;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    AudioEngine* audioEngine = nullptr;
    juce::Label masterLabel { {}, "MASTER OUT" };
    juce::Slider masterGain;
    juce::TextButton bounceButton { "BOUNCE" };
    juce::ToggleButton automationVisible { "AUTOMATION" };
    juce::Label cpuLabel;
    juce::Label deviceLabel;
    juce::Label overloadLabel;
    double cpuLoad = 0.0;
    juce::ProgressBar cpuMeter { cpuLoad };
};
