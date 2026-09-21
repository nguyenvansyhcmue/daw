#pragma once

#include <atomic>

#include "GlobalScaleContext.h"
#include "TrackMixing.h"

// Realtime-safe DSP boundary for one live vocalist. UI and project code should
// change only the atomics; preparation and processing stay on the audio path.
class VocalistProcessor final
{
public:
    void prepare(double sampleRate, int blockSize, int channels) noexcept
    {
        currentSampleRate = sampleRate;
        currentBlockSize = blockSize;
        currentChannels = channels;
        previousGain = gain.load(std::memory_order_relaxed);
    }

    void process(juce::AudioBuffer<float>& buffer,
                 const GlobalScaleContext& scaleContext) noexcept
    {
        const auto scale = scaleContext.read();
        lastScaleRevision.store(scale.revision, std::memory_order_relaxed);

        const auto targetGain = gain.load(std::memory_order_relaxed);
        const auto samples = buffer.getNumSamples();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.applyGainRamp(channel, 0, samples, previousGain, targetGain);
        previousGain = targetGain;

        TrackMixing::apply(buffer, samples,
                           { 1.0f, pan.load(std::memory_order_relaxed),
                             ! muted.load(std::memory_order_relaxed) });
    }

    void release() noexcept
    {
        currentSampleRate = 0.0;
        currentBlockSize = 0;
        currentChannels = 0;
    }

    void setGain(float value) noexcept { gain.store(juce::jlimit(0.0f, 2.0f, value), std::memory_order_relaxed); }
    void setPan(float value) noexcept { pan.store(juce::jlimit(-1.0f, 1.0f, value), std::memory_order_relaxed); }
    void setMuted(bool value) noexcept { muted.store(value, std::memory_order_relaxed); }
    float getGain() const noexcept { return gain.load(std::memory_order_relaxed); }
    uint64_t getLastScaleRevision() const noexcept { return lastScaleRevision.load(std::memory_order_relaxed); }

private:
    double currentSampleRate = 0.0;
    int currentBlockSize = 0;
    int currentChannels = 0;
    std::atomic<float> gain { 1.0f };
    std::atomic<float> pan { 0.0f };
    std::atomic<bool> muted { false };
    std::atomic<uint64_t> lastScaleRevision { 0 };
    float previousGain = 1.0f;
};
