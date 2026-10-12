#include "PluginHostService.h"
#include "../Midi/MidiTransposeProcessor.h"

namespace
{
juce::File bundledVst3Directory()
{
    // Product bundles are discovered by format and metadata. The host must
    // never route a particular product name through a private processor path.
    const auto executable = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    return executable.getParentDirectory().getParentDirectory()
        .getChildFile ("Resources").getChildFile ("PlugIns").getChildFile ("VST3");
}

juce::File vst3ProbeExecutable()
{
   #if defined(STUDIOFORGE_VST3_PROBE_PATH)
    return juce::File { STUDIOFORGE_VST3_PROBE_PATH };
   #else
    return {};
   #endif
}

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
        if (instance == nullptr)
            return;

        // VST3 keeps an independent bus model. setPlayConfigDetails alone
        // updates JUCE's legacy channel counters but does not guarantee that
        // the VST3 component has activated matching input/output buses. A
        // live mono mic is promoted to this canonical stereo track buffer.
        // Without this explicit negotiation JUCE's VST3 client mapper may be
        // handed a non-null input bus with null channel pointers.
        const auto channels = juce::jlimit(1, 2, numChannels);
        auto layout = instance->getBusesLayout();
        if (instance->getBusCount(true) > 0)
            layout.getMainInputChannelSet() = channels == 1 ? juce::AudioChannelSet::mono()
                                                             : juce::AudioChannelSet::stereo();
        if (instance->getBusCount(false) > 0)
            layout.getMainOutputChannelSet() = channels == 1 ? juce::AudioChannelSet::mono()
                                                              : juce::AudioChannelSet::stereo();

        instance->releaseResources();
        if (! instance->setBusesLayout(layout))
        {
            prepared = false;
            return;
        }

        instance->enableAllBuses();
        instance->setPlayConfigDetails(channels, channels, sampleRate, samplesPerBlock);
        instance->prepareToPlay(sampleRate, samplesPerBlock);
        expectedChannels = channels;
        prepared = true;
    }

    void processBlock(juce::AudioBuffer<float>& buffer) override
    {
        if (! prepared || instance == nullptr || buffer.getNumChannels() < expectedChannels)
            return;
        midiBuffer.clear();
        instance->processBlock(buffer, midiBuffer);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        if (! prepared || instance == nullptr || buffer.getNumChannels() < expectedChannels)
            return;
        instance->processBlock(buffer, midi);
    }

    void releaseResources() override
    {
        prepared = false;
        if (instance != nullptr)
            instance->releaseResources();
    }
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

    bool setParameterValue(const juce::String& identifier, float normalisedValue) override
    {
        if (auto* parameter = findParameter(identifier); parameter != nullptr)
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalisedValue));
            parameter->endChangeGesture();
            return true;
        }
        return false;
    }

    bool setParameterText(const juce::String& identifier, const juce::String& value) override
    {
        if (auto* parameter = findParameter(identifier); parameter != nullptr)
        {
            const auto normalised = juce::jlimit(0.0f, 1.0f, parameter->getValueForText(value));
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(normalised);
            parameter->endChangeGesture();
            return true;
        }
        return false;
    }

    juce::String getParameterText(const juce::String& identifier) const override
    {
        if (const auto* parameter = findParameter(identifier); parameter != nullptr)
            return parameter->getCurrentValueAsText();
        return {};
    }

    std::vector<EffectParameterDescriptor> getParameterDescriptors() const override
    {
        std::vector<EffectParameterDescriptor> descriptors;
        if (instance == nullptr) return descriptors;
        descriptors.reserve(static_cast<size_t>(instance->getParameters().size()));
        for (auto* parameter : instance->getParameters())
        {
            if (parameter == nullptr) continue;
            EffectParameterDescriptor descriptor;
            descriptor.name = parameter->getName(128);
            if (const auto* identified = dynamic_cast<const juce::AudioProcessorParameterWithID*>(parameter); identified != nullptr)
                descriptor.identifier = identified->getParameterID();
            if (descriptor.identifier.isEmpty()) descriptor.identifier = descriptor.name;
            descriptors.push_back(std::move(descriptor));
        }
        return descriptors;
    }

    bool hasEditor() const override
    {
        return instance != nullptr && instance->hasEditor();
    }

    std::unique_ptr<juce::Component> createEditor() override
    {
        // Never create or re-parent an AppKit plug-in view from the audio or
        // a worker thread. The caller receives no editor until the UI loop
        // owns this transition.
        auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
        if (! hasEditor() || messageManager == nullptr || ! messageManager->isThisTheMessageThread())
            return {};
        return std::unique_ptr<juce::Component>(instance->createEditorIfNeeded());
    }

private:
    juce::AudioProcessorParameter* findParameter(const juce::String& identifier) const
    {
        for (auto* parameter : instance->getParameters())
        {
            auto matches = parameter->getName(128).equalsIgnoreCase(identifier);
            if (const auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*>(parameter); withId != nullptr)
                matches = matches || withId->getParameterID().equalsIgnoreCase(identifier);
            if (matches) return parameter;
        }
        return nullptr;
    }

    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::MidiBuffer midiBuffer;
    int expectedChannels = 0;
    bool prepared = false;
};
}

PluginHostService::PluginHostService(juce::File catalogFile) : catalog(std::move(catalogFile))
{
    formatManager.addDefaultFormats();
    const auto hasSavedCatalog = catalog.existsAsFile();
    loadCatalogFromDisk();

    // Scan every product shipped in the application bundle using the same
    // protected scanner and metadata path as a user-installed VST3.
    const auto bundledDirectory = bundledVst3Directory();
    if (bundledDirectory.isDirectory())
        for (const auto& bundle : bundledDirectory.findChildFiles(juce::File::findDirectories, false, "*.vst3"))
            scanVst3Async(bundle, {});

   #if !defined(STUDIOFORGE_DISABLE_AUTOMATIC_PLUGIN_SCAN)
    scanDefaultVst3LocationsAsync();
   #endif
   #if !defined(STUDIOFORGE_DISABLE_AUTOMATIC_PLUGIN_SCAN) && JUCE_MAC
    // Populate an empty catalog on first launch only. Re-scanning every launch
    // is costly and can unnecessarily contend with plug-in creation.
    if (! hasSavedCatalog)
        scanAudioUnitsAsync();
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

    // JUCE's VST3 scanner wants the .vst3 bundle on macOS, while callers may
    // reasonably hand us its executable module (for example a file chooser
    // or a generated test target). Normalise that form on the control thread.
    auto scanTarget = bundleOrModule;
    auto parent = scanTarget.getParentDirectory();
    for (auto depth = 0; ! scanTarget.hasFileExtension(".vst3") && parent.exists() && depth++ < 8;
         parent = parent.getParentDirectory())
    {
        scanTarget = parent;
    }
    if (! scanTarget.hasFileExtension(".vst3"))
        return juce::Result::fail("Selected file is not inside a VST3 bundle");

    auto* vst3Format = findVst3Format();
    if (vst3Format == nullptr)
        return juce::Result::fail("VST3 hosting is not enabled in this build");
    if (knownPlugins.isListingUpToDate(scanTarget.getFullPathName(), *vst3Format))
        return juce::Result::ok();

    const auto probe = vst3ProbeExecutable();
    if (! probe.existsAsFile())
        return juce::Result::fail("VST3 scanner helper is unavailable in this StudioForge build");

    auto resultDirectory = catalog.getParentDirectory();
    if (! resultDirectory.createDirectory())
        return juce::Result::fail("Cannot prepare the VST3 scan result directory");
    const auto resultFile = resultDirectory.getNonexistentChildFile(".vst3-probe", ".xml", false);
    if (! resultFile.replaceWithText({}))
        return juce::Result::fail("Cannot create a protected VST3 scan result file");
    juce::ChildProcess process;
    if (! process.start(juce::StringArray { probe.getFullPathName(), scanTarget.getFullPathName(), resultFile.getFullPathName() }))
        return juce::Result::fail("Could not start the protected VST3 scanner");
    if (! process.waitForProcessToFinish(15000))
    {
        process.kill();
        resultFile.deleteFile();
        knownPlugins.addToBlacklist(scanTarget.getFullPathName());
        saveCatalogToDisk();
        return juce::Result::fail("VST3 scan timed out and was quarantined: " + scanTarget.getFileName());
    }
    if (process.getExitCode() != 0 || ! resultFile.existsAsFile())
    {
        const auto exitCode = process.getExitCode();
        const auto hasResult = resultFile.existsAsFile();
        knownPlugins.addToBlacklist(scanTarget.getFullPathName());
        saveCatalogToDisk();
        resultFile.deleteFile();
        return juce::Result::fail("VST3 scan failed safely and was quarantined: " + scanTarget.getFileName()
                                  + " (exit " + juce::String(static_cast<int>(exitCode))
                                  + ", metadata " + (hasResult ? "present" : "missing") + ")");
    }

    const auto document = juce::parseXML(resultFile);
    resultFile.deleteFile();
    if (document == nullptr || ! document->hasTagName("KNOWNPLUGINS"))
        return juce::Result::fail("VST3 scanner returned invalid plug-in metadata");
    auto found = false;
    for (const auto* element : document->getChildIterator())
    {
        juce::PluginDescription description;
        if (description.loadFromXml(*element))
        {
            knownPlugins.addType(description);
            found = true;
        }
    }
    if (! found)
        return juce::Result::fail("No VST3 plug-in type was found");
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
    juce::StringArray failed;
    for (const auto& candidate : candidates)
        if (const auto result = scanVst3(juce::File { candidate }); result.failed())
            failed.add(result.getErrorMessage());

    saveCatalogToDisk();
    return failed.isEmpty() ? juce::Result::ok()
                            : juce::Result::fail("Some VST3 plug-ins were skipped safely: " + failed.joinIntoString(" | "));
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

    juce::Array<juce::PluginDescription> matches;
    for (const auto& plugin : allPlugins)
        if (normalisedQuery.isEmpty()
            || plugin.name.toLowerCase().contains(normalisedQuery)
            || plugin.category.toLowerCase().contains(normalisedQuery)
            || plugin.manufacturerName.toLowerCase().contains(normalisedQuery))
        {
            auto existingIndex = -1;
            for (int index = 0; index < matches.size(); ++index)
            {
                const auto& existing = matches.getReference(index);
                if (existing.manufacturerName.trim().equalsIgnoreCase(plugin.manufacturerName.trim())
                    && existing.name.trim().equalsIgnoreCase(plugin.name.trim()))
                {
                    existingIndex = index;
                    break;
                }
            }

            if (existingIndex < 0)
            {
                matches.add(plugin);
                continue;
            }

            // A VST3 entry is the preferred cross-platform route. If both
            // formats exist, keep the VST3 bundle that actually remains on disk.
            const auto& existing = matches.getReference(existingIndex);
            const auto existingIsVst3 = existing.pluginFormatName.equalsIgnoreCase("VST3");
            const auto candidateIsVst3 = plugin.pluginFormatName.equalsIgnoreCase("VST3");
            const auto existingExists = existingIsVst3 && juce::File { existing.fileOrIdentifier }.exists();
            const auto candidateExists = candidateIsVst3 && juce::File { plugin.fileOrIdentifier }.exists();
            if ((candidateIsVst3 && ! existingIsVst3)
                || (! existingExists && candidateExists))
                matches.set(existingIndex, plugin);
        }
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
