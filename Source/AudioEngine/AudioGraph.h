#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <memory>
#include <vector>

class AudioNode
{
public:
    virtual ~AudioNode() = default;
    virtual void process(juce::AudioBuffer<float>& buffer, int numSamples) = 0;
};

class GainNode final : public AudioNode
{
public:
    explicit GainNode(float initialGain = 1.0f) : gain(initialGain) {}

    void setGain(float newGain) noexcept
    {
        gain.store(newGain);
    }

    void process(juce::AudioBuffer<float>& buffer, int numSamples) override;

private:
    std::atomic<float> gain { 1.0f };
};

class AudioGraph final
{
public:
    AudioGraph() = default;

    void addNode(std::unique_ptr<AudioNode> node);
    void clear() noexcept;
    void setMasterGain(float value) noexcept;
    float getMasterGain() const noexcept;

    void process(juce::AudioBuffer<float>& buffer, int numSamples);

private:
    std::vector<std::unique_ptr<AudioNode>> nodes;
    std::atomic<float> masterGain { 1.0f };
};
