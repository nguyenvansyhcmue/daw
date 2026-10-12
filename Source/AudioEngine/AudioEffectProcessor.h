#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <memory>
#include <vector>

struct EffectParameterDescriptor
{
    juce::String identifier;
    juce::String name;
};

enum class LiveProcessorState : uint8_t
{
    unavailable = 0,
    ready,
    priming,
    tracking,
    protectedDry,
    holding
};

// Lock-free snapshot read by the message thread. Implementations publish only
// atomics from the audio callback; no strings or allocations cross that path.
struct LiveProcessorHealth
{
    LiveProcessorState state = LiveProcessorState::unavailable;
    float wetMix = 0.0f;
    bool voiced = false;
    bool trackerVoiced = false;
    float trackerConfidence = 0.0f;
    float trackerF0Hz = 0.0f;
    int trackerDetectedMidiNote = -1;
    int trackerTargetMidiNote = -1;
    int trackerOutputMidiNote = -1;
    float trackerInputCents = 0.0f;
    float correctionCents = 0.0f;
    bool rendererCollapsed = false;
    bool rendererExcessive = false;
    bool rendererInvalid = false;
    bool callbackOverloaded = false;
    bool hardDryRescue = false;
    uint64_t protectionCount = 0;
    uint64_t overloadCount = 0;
};

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
    // A vocal processor explicitly designed for live monitoring may exceed
    // the generic look-ahead FX budget while still being required in Stage.
    // Hosts must opt in deliberately; the default remains protected.
    virtual bool isLivePerformanceEssential() const noexcept { return false; }
    // Product-owned processors can identify themselves without relying on a
    // persistent plug-in identifier, which is shared by legacy AU/VST3 hosts.
    virtual bool isStudioForgeNativeVibeAutotune() const noexcept { return false; }
    virtual LiveProcessorHealth getLiveProcessorHealth() const noexcept { return {}; }
    virtual juce::String getPersistentIdentifier() const { return {}; }
    virtual double getTailLengthSeconds() const { return 0.0; }
    virtual bool getState(juce::MemoryBlock&) const { return false; }
    virtual bool setState(const void*, size_t) { return false; }
    virtual bool setParameterValue(size_t, float) { return false; }
    // Control-thread parameter access used by deterministic render tools. The
    // regular editor continues to own interactive automation.
    virtual bool setParameterValue(const juce::String&, float) { return false; }
    virtual bool setParameterText(const juce::String&, const juce::String&) { return false; }
    virtual juce::String getParameterText(const juce::String&) const { return {}; }
    virtual std::vector<EffectParameterDescriptor> getParameterDescriptors() const { return {}; }
    // Optional host-native key control. Implementations return false when the
    // plug-in does not expose a compatible musical-key parameter pair.
    virtual bool applyDetectedKey(int, bool) { return false; }
    virtual bool hasEditor() const { return false; }
    virtual std::unique_ptr<juce::Component> createEditor() { return {}; }
};
