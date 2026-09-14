#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

struct TrackMixSettings
{
    float gain = 1.0f;
    float pan = 0.0f;
    bool audible = true;
};

class TrackMixing final
{
public:
    static void apply(juce::AudioBuffer<float>& buffer, int numSamples,
                      const TrackMixSettings& settings) noexcept
    {
        const auto samples = juce::jlimit(0, buffer.getNumSamples(), numSamples);
        if (samples == 0)
            return;

        if (! settings.audible)
        {
            buffer.clear(0, samples);
            return;
        }

        const auto gain = juce::jmax(0.0f, settings.gain);
        const auto pan = juce::jlimit(-1.0f, 1.0f, settings.pan);
        const auto leftGain = gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
        const auto rightGain = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);

        if (buffer.getNumChannels() == 1)
        {
            buffer.applyGain(0, 0, samples, gain);
            return;
        }

        buffer.applyGain(0, 0, samples, leftGain);
        buffer.applyGain(1, 0, samples, rightGain);
        for (int channel = 2; channel < buffer.getNumChannels(); ++channel)
            buffer.applyGain(channel, 0, samples, gain);
    }

    static float peak(const juce::AudioBuffer<float>& buffer, int numSamples) noexcept
    {
        const auto samples = juce::jlimit(0, buffer.getNumSamples(), numSamples);
        float result = 0.0f;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax(
                buffer.getReadPointer(channel), samples);
            result = juce::jmax(result, juce::jmax(std::abs(range.getStart()), std::abs(range.getEnd())));
        }
        return result;
    }
};
