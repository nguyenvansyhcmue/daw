#pragma once

#include "../AudioEngine/AudioEffectProcessor.h"

class MidiTransposeProcessor final : public AudioEffectProcessor
{
public:
    explicit MidiTransposeProcessor(int semitones = 0) noexcept;

    void prepareToPlay(double sampleRate, int samplesPerBlock, int numChannels) override;
    void processBlock(juce::AudioBuffer<float>& buffer) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void releaseResources() override;
    juce::String getName() const override;
    juce::String getPersistentIdentifier() const override;
    bool getState(juce::MemoryBlock& state) const override;
    bool setState(const void* data, size_t size) override;
    bool isMidiEffect() const override;

    void setSemitones(int value) noexcept;
    int getSemitones() const noexcept;

private:
    int semitones = 0;
    juce::MidiBuffer transformedMidi;
};
