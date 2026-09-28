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

class AudioUnitScanJob final : public juce::ThreadPoolJob
{
public:
    AudioUnitScanJob(PluginHostService& owner, std::function<void(juce::Result)> finished)
        : juce::ThreadPoolJob("StudioForge Audio Unit scan"), host(owner), completion(std::move(finished)) {}

    JobStatus runJob() override
    {
        const auto result = host.scanAudioUnits();
        if (completion != nullptr)
            juce::MessageManager::callAsync([completion = std::move(completion), result]() mutable { completion(result); });
        return jobHasFinished;
    }

private:
    PluginHostService& host;
    std::function<void(juce::Result)> completion;
};

class DefaultVst3ScanJob final : public juce::ThreadPoolJob
{
public:
    DefaultVst3ScanJob(PluginHostService& owner, std::function<void(juce::Result)> finished)
        : juce::ThreadPoolJob("StudioForge default VST3 scan"), host(owner), completion(std::move(finished)) {}

    JobStatus runJob() override
    {
        const auto result = host.scanDefaultVst3Locations();
        if (completion != nullptr)
            juce::MessageManager::callAsync([completion = std::move(completion), result]() mutable { completion(result); });
        return jobHasFinished;
    }

private:
    PluginHostService& host;
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
    int getLatencySamples() const noexcept override
    {
        return instance != nullptr ? juce::jmax(0, instance->getLatencySamples()) : 0;
    }
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

    bool applyDetectedKey(int rootNote, bool minor) override
    {
        if (instance == nullptr || ! instance->getName().containsIgnoreCase("VibeAutotune"))
            return false;

        static constexpr std::array<const char*, 12> noteNames {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        return setParameterText("root", noteNames[static_cast<size_t>(juce::jlimit(0, 11, rootNote))])
            && setParameterText("scale", minor ? "Natural Minor" : "Major");
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
    bool setParameterText(const juce::String& identifier, const juce::String& value)
    {
        for (auto* parameter : instance->getParameters())
        {
            auto matches = parameter->getName(128).equalsIgnoreCase(identifier);
            if (const auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*>(parameter); withId != nullptr)
                matches = matches || withId->getParameterID().equalsIgnoreCase(identifier);
            if (! matches) continue;

            const auto normalised = juce::jlimit(0.0f, 1.0f, parameter->getValueForText(value));
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(normalised);
            parameter->endChangeGesture();
            return true;
        }
        return false;
    }

    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::MidiBuffer midiBuffer;
};
}

PluginHostService::PluginHostService(juce::File catalogFile) : catalog(std::move(catalogFile))
{
    formatManager.addDefaultFormats();
    const auto hasSavedCatalog = catalog.existsAsFile();
    loadCatalogFromDisk();
   #if JUCE_MAC
    // Populate an empty catalog on first launch only. Re-scanning every launch
    // is costly and can unnecessarily contend with plug-in creation.
    if (! hasSavedCatalog)
        scanAudioUnitsAsync();
   #else
    // VST3 is the common cross-platform format. JUCE resolves Windows'
    // per-user and system Common Files VST3 locations automatically.
    if (! hasSavedCatalog)
        scanDefaultVst3LocationsAsync();
   #endif
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

    auto* vst3Format = findVst3Format();
    if (vst3Format == nullptr)
        return juce::Result::fail("VST3 hosting is not enabled in this build");
    juce::OwnedArray<juce::PluginDescription> found;
    if (! knownPlugins.scanAndAddFile(bundleOrModule.getFullPathName(), true, found, *vst3Format))
        return juce::Result::fail("No VST3 plugin type was found");
    saveCatalogToDisk();
    return juce::Result::ok();
}

void PluginHostService::scanVst3Async(juce::File bundleOrModule, std::function<void(juce::Result)> completion)
{
    scanPool.addJob(new PluginScanJob(*this, std::move(bundleOrModule), std::move(completion)), true);
}

juce::Result PluginHostService::scanDefaultVst3Locations()
{
    auto* vst3Format = findVst3Format();
    if (vst3Format == nullptr)
        return juce::Result::fail("VST3 hosting is not enabled in this build");

    const auto candidates = vst3Format->searchPathsForPlugins(vst3Format->getDefaultLocationsToSearch(), true, false);
    juce::OwnedArray<juce::PluginDescription> found;
    const juce::ScopedLock lock(catalogLock);
    for (const auto& candidate : candidates)
        knownPlugins.scanAndAddFile(candidate, true, found, *vst3Format);

    saveCatalogToDisk();
    return juce::Result::ok();
}

void PluginHostService::scanDefaultVst3LocationsAsync(std::function<void(juce::Result)> completion)
{
    scanPool.addJob(new DefaultVst3ScanJob(*this, std::move(completion)), true);
}

juce::Result PluginHostService::scanAudioUnits()
{
    auto* audioUnitFormat = findAudioUnitFormat();
    if (audioUnitFormat == nullptr)
        return juce::Result::fail("Audio Unit hosting is not enabled in this build");

    // Audio Units are registered with Core Audio rather than discovered only
    // from .component bundles, so ask the format for every registered AU ID.
    const auto identifiers = audioUnitFormat->searchPathsForPlugins({}, false, false);
    juce::OwnedArray<juce::PluginDescription> found;
    for (const auto& identifier : identifiers)
        knownPlugins.scanAndAddFile(identifier, true, found, *audioUnitFormat);

    {
        const juce::ScopedLock lock(catalogLock);
        saveCatalogToDisk();
    }
    return juce::Result::ok();
}

void PluginHostService::scanAudioUnitsAsync(std::function<void(juce::Result)> completion)
{
    scanPool.addJob(new AudioUnitScanJob(*this, std::move(completion)), true);
}

bool PluginHostService::supportsAudioUnits() const noexcept
{
    return findAudioUnitFormat() != nullptr;
}

juce::Array<juce::PluginDescription> PluginHostService::getKnownPlugins() const
{
    return copyKnownPlugins();
}

juce::Array<juce::PluginDescription> PluginHostService::findKnownPlugins(const juce::String& query) const
{
    const auto normalisedQuery = query.trim().toLowerCase();
    const auto allPlugins = copyKnownPlugins();
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

void PluginHostService::loadCatalogFromDisk()
{
    if (! catalog.existsAsFile())
        return;

    if (const auto document = juce::parseXML(catalog); document != nullptr)
        knownPlugins.recreateFromXml(*document);
}

void PluginHostService::saveCatalogToDisk() const
{
    if (! catalog.getParentDirectory().exists())
        catalog.getParentDirectory().createDirectory();
    if (const auto document = knownPlugins.createXml(); document != nullptr)
        document->writeTo(catalog);
}

juce::AudioPluginFormat* PluginHostService::findVst3Format() const noexcept
{
    for (auto* format : formatManager.getFormats())
        if (format != nullptr && format->getName().equalsIgnoreCase("VST3"))
            return format;
    return nullptr;
}

juce::AudioPluginFormat* PluginHostService::findAudioUnitFormat() const noexcept
{
    for (auto* format : formatManager.getFormats())
        if (format != nullptr && format->getName().equalsIgnoreCase("AudioUnit"))
            return format;
    return nullptr;
}

juce::Array<juce::PluginDescription> PluginHostService::copyKnownPlugins() const
{
    const juce::ScopedLock lock(catalogLock);
    return knownPlugins.getTypes();
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
