#pragma once

#include "AudioEffectProcessor.h"

#include <atomic>

class GainUtilityProcessor final : public AudioEffectProcessor
{
public:
    void prepareToPlay(double, int, int) override
    {
        currentGain.store(0.5f, std::memory_order_relaxed);
        lastGain = 0.5f;
    }

    void processBlock(juce::AudioBuffer<float>& buffer) override
    {
        const auto targetGain = currentGain.load(std::memory_order_relaxed);
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.applyGainRamp(channel, 0, buffer.getNumSamples(), lastGain, targetGain);
        lastGain = targetGain;
    }

    void releaseResources() override {}
    juce::String getName() const override { return "Gain Utility (-6dB)"; }

private:
    std::atomic<float> currentGain { 0.5f };
    float lastGain = 0.5f;
};
