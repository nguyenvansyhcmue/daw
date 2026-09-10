#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

class AudioEffectProcessor
{
public:
    virtual ~AudioEffectProcessor() = default;

    virtual void prepareToPlay(double sampleRate, int samplesPerBlock, int numChannels) = 0;
    virtual void processBlock(juce::AudioBuffer<float>& buffer) = 0;
    virtual void releaseResources() = 0;
    virtual juce::String getName() const = 0;
    virtual double getTailLengthSeconds() const { return 0.0; }
};
