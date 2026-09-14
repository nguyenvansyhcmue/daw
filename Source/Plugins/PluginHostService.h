#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../AudioEngine/AudioEffectProcessor.h"

// Discovery and construction are control-thread operations. The prepared
// adapter is later published through the existing immutable FX rack snapshot.
class PluginHostService final
{
public:
    PluginHostService();

    juce::Result scanVst3(const juce::File& bundleOrModule);
    juce::Array<juce::PluginDescription> getKnownPlugins() const;
    std::shared_ptr<AudioEffectProcessor> createEffect(const juce::PluginDescription& description,
                                                       double sampleRate, int blockSize,
                                                       juce::String& errorMessage) const;

private:
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
};
