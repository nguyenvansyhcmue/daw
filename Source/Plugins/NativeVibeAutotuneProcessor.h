#pragma once

#include "LiveTuneConfidenceGate.h"
#include "../AudioEngine/AudioEffectProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

class VibeAutotuneProcessor;

// StudioForge owns this processor directly. It deliberately reuses the exact
// Vibe processor/editor sources rather than hosting the bundled VST3 wrapper,
// whose macOS view bridge is not safe to embed in this app's window hierarchy.
class NativeVibeAutotuneProcessor final : public AudioEffectProcessor
{
public:
    explicit NativeVibeAutotuneProcessor(juce::String persistentIdentifier);
    ~NativeVibeAutotuneProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock, int numChannels) override;
    void processBlock(juce::AudioBuffer<float>& buffer) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void releaseResources() override;
    juce::String getName() const override;
    int getLatencySamples() const noexcept override;
    bool isLivePerformanceEssential() const noexcept override { return true; }
    bool isStudioForgeNativeVibeAutotune() const noexcept override { return true; }
    LiveProcessorHealth getLiveProcessorHealth() const noexcept override;
    juce::String getPersistentIdentifier() const override;
    double getTailLengthSeconds() const override;
    bool getState(juce::MemoryBlock& state) const override;
    bool setState(const void* data, size_t size) override;
    bool setParameterValue(size_t index, float normalisedValue) override;
    bool setParameterValue(const juce::String& identifier, float normalisedValue) override;
    bool setParameterText(const juce::String& identifier, const juce::String& value) override;
    juce::String getParameterText(const juce::String& identifier) const override;
    std::vector<EffectParameterDescriptor> getParameterDescriptors() const override;
    bool hasEditor() const override;
    std::unique_ptr<juce::Component> createEditor() override;

private:
    juce::AudioProcessorParameter* findParameter(const juce::String& identifier) const;
    void processAndProtect(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept;

    std::unique_ptr<VibeAutotuneProcessor> processor;
    juce::MidiBuffer midiBuffer;
    juce::AudioBuffer<float> dryInputBuffer;
    juce::AudioBuffer<float> dryHistoryBuffer;
    juce::AudioBuffer<float> alignedDryBuffer;
    juce::String identifier;
    int expectedChannels = 0;
    double preparedSampleRate = 0.0;
    float wetMix = 0.0f;
    int dryHistoryWrite = 0;
    int dryHistoryLatency = 0;
    int64_t dryHistorySamples = 0;
    int healthyWetBlocks = 0;
    int consecutiveOverloads = 0;
    int continuityTailSamples = 0;
    LiveTuneConfidenceGate confidenceGate;
    bool wasBypassed = true;
    bool prepared = false;

    std::atomic<LiveProcessorState> liveState { LiveProcessorState::ready };
    std::atomic<float> publishedWetMix { 0.0f };
    std::atomic<bool> publishedVoiced { false };
    std::atomic<bool> publishedTrackerVoiced { false };
    std::atomic<float> publishedTrackerConfidence { 0.0f };
    std::atomic<float> publishedTrackerF0Hz { 0.0f };
    std::atomic<int> publishedTrackerDetectedMidiNote { -1 };
    std::atomic<int> publishedTrackerTargetMidiNote { -1 };
    std::atomic<int> publishedTrackerOutputMidiNote { -1 };
    std::atomic<float> publishedTrackerInputCents { 0.0f };
    std::atomic<float> publishedCorrectionCents { 0.0f };
    std::atomic<bool> publishedRendererCollapsed { false };
    std::atomic<bool> publishedRendererExcessive { false };
    std::atomic<bool> publishedRendererInvalid { false };
    std::atomic<bool> publishedCallbackOverloaded { false };
    std::atomic<bool> publishedHardDryRescue { false };
    std::atomic<uint64_t> protectionCount { 0 };
    std::atomic<uint64_t> overloadCount { 0 };
};
