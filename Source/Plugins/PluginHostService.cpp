#include "PluginHostService.h"
#include "../Midi/MidiTransposeProcessor.h"

namespace
{
class PluginScanJob final : public juce::ThreadPoolJob
{
public:
    PluginScanJob(PluginHostService& owner, juce::File source, std::function<void(juce::Result)> finished)
        : juce::ThreadPoolJob("StudioForge VST3 scan"), host(owner), pluginFile(std::move(source)), completion(std::move(finished)) {}

    JobStatus runJob() override
    {
        const auto result = host.scanVst3(pluginFile);
        juce::MessageManager::callAsync([completion = std::move(completion), result]() mutable
        {
            if (completion != nullptr) completion(result);
        });
        return jobHasFinished;
    }

private:
    PluginHostService& host;
    juce::File pluginFile;
    std::function<void(juce::Result)> completion;
};

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
    juce::String getPersistentIdentifier() const override
    {
        return instance != nullptr ? instance->getPluginDescription().createIdentifierString() : juce::String {};
    }
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

    bool hasEditor() const override
    {
        return instance != nullptr && instance->hasEditor();
    }

    std::unique_ptr<juce::Component> createEditor() override
    {
        if (! hasEditor())
            return {};
        return std::unique_ptr<juce::Component>(instance->createEditorIfNeeded());
    }

private:
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::MidiBuffer midiBuffer;
};
}

PluginHostService::PluginHostService(juce::File catalogFile) : catalog(std::move(catalogFile))
{
    formatManager.addDefaultFormats();
    loadCatalog();
}

juce::File PluginHostService::defaultCatalogFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("StudioForge")
        .getChildFile("PluginCatalog.xml");
}

juce::Result PluginHostService::scanVst3(const juce::File& bundleOrModule)
{
    const juce::ScopedLock lock(catalogLock);
    if (! bundleOrModule.exists())
        return juce::Result::fail("VST3 file or bundle does not exist");

    for (auto* format : formatManager.getFormats())
        if (format != nullptr && format->getName().equalsIgnoreCase("VST3"))
        {
            juce::OwnedArray<juce::PluginDescription> found;
            if (knownPlugins.scanAndAddFile(bundleOrModule.getFullPathName(), true, found, *format))
            {
                saveCatalog();
                return juce::Result::ok();
            }
            return juce::Result::fail("No VST3 plugin type was found");
        }

    return juce::Result::fail("VST3 hosting is not enabled in this build");
}

void PluginHostService::scanVst3Async(juce::File bundleOrModule, std::function<void(juce::Result)> completion)
{
    scanPool.addJob(new PluginScanJob(*this, std::move(bundleOrModule), std::move(completion)), true);
}

juce::Array<juce::PluginDescription> PluginHostService::getKnownPlugins() const
{
    const juce::ScopedLock lock(catalogLock);
    return knownPlugins.getTypes();
}

juce::Array<juce::PluginDescription> PluginHostService::findKnownPlugins(const juce::String& query) const
{
    const juce::ScopedLock lock(catalogLock);
    const auto normalisedQuery = query.trim().toLowerCase();
    const auto allPlugins = knownPlugins.getTypes();
    if (normalisedQuery.isEmpty())
        return allPlugins;

    juce::Array<juce::PluginDescription> matches;
    for (const auto& plugin : allPlugins)
        if (plugin.name.toLowerCase().contains(normalisedQuery)
            || plugin.category.toLowerCase().contains(normalisedQuery)
            || plugin.manufacturerName.toLowerCase().contains(normalisedQuery))
            matches.add(plugin);
    return matches;
}

void PluginHostService::loadCatalog()
{
    if (! catalog.existsAsFile())
        return;

    if (const auto document = juce::parseXML(catalog); document != nullptr)
        knownPlugins.recreateFromXml(*document);
}

void PluginHostService::saveCatalog() const
{
    if (! catalog.getParentDirectory().exists())
        catalog.getParentDirectory().createDirectory();
    if (const auto document = knownPlugins.createXml(); document != nullptr)
        document->writeTo(catalog);
}

std::shared_ptr<AudioEffectProcessor> PluginHostService::createEffect(const juce::PluginDescription& description,
                                                                        double sampleRate, int blockSize,
                                                                        juce::String& errorMessage) const
{
    const juce::ScopedLock lock(catalogLock);
    auto instance = formatManager.createPluginInstance(description, sampleRate, blockSize, errorMessage);
    if (instance == nullptr)
        return {};

    auto effect = std::make_shared<PluginEffectProcessor>(std::move(instance));
    effect->prepareToPlay(sampleRate, blockSize, 2);
    return effect;
}

std::shared_ptr<AudioEffectProcessor> PluginHostService::createEffect(const juce::String& persistentIdentifier,
                                                                        double sampleRate, int blockSize,
                                                                        juce::String& errorMessage) const
{
    if (persistentIdentifier == "studioforge.midi.transpose")
    {
        auto effect = std::make_shared<MidiTransposeProcessor>();
        effect->prepareToPlay(sampleRate, blockSize, 2);
        return effect;
    }

    const auto plugins = getKnownPlugins();
    for (const auto& description : plugins)
        if (description.createIdentifierString() == persistentIdentifier)
            return createEffect(description, sampleRate, blockSize, errorMessage);

    errorMessage = "Plug-in is not available: " + persistentIdentifier;
    return {};
}
