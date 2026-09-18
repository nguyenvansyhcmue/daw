#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

class AudioEffectProcessor
{
public:
    virtual ~AudioEffectProcessor() = default;

    virtual void prepareToPlay(double sampleRate, int samplesPerBlock, int numChannels) = 0;
    virtual void processBlock(juce::AudioBuffer<float>& buffer) = 0;
    virtual void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { processBlock(buffer); }
    virtual void releaseResources() = 0;
    virtual juce::String getName() const = 0;
    virtual bool isMidiEffect() const { return false; }
    virtual juce::String getPersistentIdentifier() const { return {}; }
    virtual double getTailLengthSeconds() const { return 0.0; }
    virtual bool getState(juce::MemoryBlock&) const { return false; }
    virtual bool setState(const void*, size_t) { return false; }
    virtual bool setParameterValue(size_t, float) { return false; }
    virtual bool hasEditor() const { return false; }
    virtual std::unique_ptr<juce::Component> createEditor() { return {}; }
};
