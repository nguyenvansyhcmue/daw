#include "PluginHostService.h"

namespace
{
class PluginEffectProcessor final : public AudioEffectProcessor
{
public:
    explicit PluginEffectProcessor(std::unique_ptr<juce::AudioPluginInstance> instanceIn)
        : instance(std::move(instanceIn)) {}

    void prepareToPlay(double sampleRate, int samplesPerBlock, int numChannels) override
    {
        instance->releaseResources();
        instance->setPlayConfigDetails(numChannels, numChannels, sampleRate, samplesPerBlock);
        instance->prepareToPlay(sampleRate, samplesPerBlock);
    }

    void processBlock(juce::AudioBuffer<float>& buffer) override
    {
        midiBuffer.clear();
        instance->processBlock(buffer, midiBuffer);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        instance->processBlock(buffer, midi);
    }

    void releaseResources() override { instance->releaseResources(); }
    juce::String getName() const override { return instance->getName(); }
    double getTailLengthSeconds() const override { return instance->getTailLengthSeconds(); }
    bool getState(juce::MemoryBlock& state) const override
    {
        instance->getStateInformation(state);
        return ! state.isEmpty();
    }

    bool setState(const void* data, size_t size) override
    {
        if (data == nullptr || size == 0) return false;
        instance->setStateInformation(data, static_cast<int>(size));
        return true;
    }

    bool setParameterValue(size_t index, float normalisedValue) override
    {
        const auto& parameters = instance->getParameters();
        if (index >= static_cast<size_t>(parameters.size())) return false;
        parameters[static_cast<int>(index)]->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalisedValue));
        return true;
    }

private:
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::MidiBuffer midiBuffer;
};
}

PluginHostService::PluginHostService()
{
    formatManager.addDefaultFormats();
}

juce::Result PluginHostService::scanVst3(const juce::File& bundleOrModule)
{
    if (! bundleOrModule.exists())
        return juce::Result::fail("VST3 file or bundle does not exist");

    for (auto* format : formatManager.getFormats())
        if (format != nullptr && format->getName().equalsIgnoreCase("VST3"))
        {
            juce::OwnedArray<juce::PluginDescription> found;
            if (knownPlugins.scanAndAddFile(bundleOrModule.getFullPathName(), true, found, *format))
                return juce::Result::ok();
            return juce::Result::fail("No VST3 plugin type was found");
        }

    return juce::Result::fail("VST3 hosting is not enabled in this build");
}

juce::Array<juce::PluginDescription> PluginHostService::getKnownPlugins() const
{
    return knownPlugins.getTypes();
}

std::shared_ptr<AudioEffectProcessor> PluginHostService::createEffect(const juce::PluginDescription& description,
                                                                        double sampleRate, int blockSize,
                                                                        juce::String& errorMessage) const
{
    auto instance = formatManager.createPluginInstance(description, sampleRate, blockSize, errorMessage);
    if (instance == nullptr)
        return {};

    auto effect = std::make_shared<PluginEffectProcessor>(std::move(instance));
    effect->prepareToPlay(sampleRate, blockSize, 2);
    return effect;
}
