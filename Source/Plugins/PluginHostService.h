#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

#include "../AudioEngine/AudioEffectProcessor.h"

// Discovery and construction are control-thread operations. The prepared
// adapter is later published through the existing immutable FX rack snapshot.
class PluginHostService final
{
public:
    explicit PluginHostService(juce::File catalogFile = defaultCatalogFile());

    juce::Result scanVst3(const juce::File& bundleOrModule);
    void scanVst3Async(juce::File bundleOrModule, std::function<void(juce::Result)> completion);
    juce::Array<juce::PluginDescription> getKnownPlugins() const;
    juce::Array<juce::PluginDescription> findKnownPlugins(const juce::String& query) const;
    std::shared_ptr<AudioEffectProcessor> createEffect(const juce::PluginDescription& description,
                                                       double sampleRate, int blockSize,
                                                       juce::String& errorMessage) const;
    std::shared_ptr<AudioEffectProcessor> createEffect(const juce::String& persistentIdentifier,
                                                       double sampleRate, int blockSize,
                                                       juce::String& errorMessage) const;

    static juce::File defaultCatalogFile();

private:
    void loadCatalog();
    void saveCatalog() const;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    juce::File catalog;
    mutable juce::CriticalSection catalogLock;
    juce::ThreadPool scanPool { 1 };
};
