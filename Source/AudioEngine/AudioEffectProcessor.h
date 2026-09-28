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
    // Reported by plug-in hosts for look-ahead processors and linear-phase FX.
    // The live path uses it for readiness diagnostics.
    virtual int getLatencySamples() const noexcept { return 0; }
    virtual juce::String getPersistentIdentifier() const { return {}; }
    virtual double getTailLengthSeconds() const { return 0.0; }
    virtual bool getState(juce::MemoryBlock&) const { return false; }
    virtual bool setState(const void*, size_t) { return false; }
    virtual bool setParameterValue(size_t, float) { return false; }
    // Optional host-native key control. Implementations return false when the
    // plug-in does not expose a compatible musical-key parameter pair.
    virtual bool applyDetectedKey(int, bool) { return false; }
    virtual bool hasEditor() const { return false; }
    virtual std::unique_ptr<juce::Component> createEditor() { return {}; }
};
