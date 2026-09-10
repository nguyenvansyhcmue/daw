#include "AudioGraph.h"

#include <cmath>

void GainNode::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (numSamples <= 0)
        return;

    const auto currentGain = gain.load();
    if (std::abs(currentGain - 1.0f) < 1.0e-5f)
        return;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* const data = buffer.getWritePointer(channel);
        for (int sample = 0; sample < numSamples; ++sample)
            data[sample] *= currentGain;
    }
}

void AudioGraph::addNode(std::unique_ptr<AudioNode> node)
{
    if (node != nullptr)
        nodes.push_back(std::move(node));
}

void AudioGraph::clear() noexcept
{
    nodes.clear();
}

void AudioGraph::setMasterGain(float value) noexcept
{
    masterGain.store(juce::jlimit(0.0f, 2.0f, value));
}

float AudioGraph::getMasterGain() const noexcept
{
    return masterGain.load();
}

void AudioGraph::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (numSamples <= 0)
        return;

    if (nodes.empty())
        return;

    for (auto& node : nodes)
        node->process(buffer, numSamples);

    const auto master = masterGain.load();
    if (std::abs(master - 1.0f) < 1.0e-5f)
        return;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* const data = buffer.getWritePointer(channel);
        for (int sample = 0; sample < numSamples; ++sample)
            data[sample] *= master;
    }
}
