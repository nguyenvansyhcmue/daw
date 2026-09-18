#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>

class AudioPeakMeter final : public juce::Component, private juce::Timer
{
public:
    AudioPeakMeter();

    void paint(juce::Graphics& g) override;
    void resized() override {}

    void updatePeak(float newLevel) noexcept;
    void updateStereoPeak(float leftLevel, float rightLevel) noexcept;
    void resetClipIndicator() noexcept;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    std::atomic<float> currentPeak { 0.0f };
    std::atomic<float> currentRightPeak { 0.0f };
    std::atomic<bool> clipLatched { false };
    float displayedPeak = 0.0f;
    float displayedRightPeak = 0.0f;
};
