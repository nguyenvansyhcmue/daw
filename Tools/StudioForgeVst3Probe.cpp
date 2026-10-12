#include <juce_audio_processors/juce_audio_processors.h>

#include <iostream>

int main(int argc, char* argv[])
{
    if (argc != 3)
        return 64;

    const juce::File bundle { argv[1] };
    const juce::File output { argv[2] };
    if (! bundle.exists() || ! bundle.hasFileExtension(".vst3"))
        return 65;

    juce::AudioPluginFormatManager formats;
    formats.addDefaultFormats();
    for (auto* format : formats.getFormats())
    {
        if (format == nullptr || ! format->getName().equalsIgnoreCase("VST3"))
            continue;

        juce::OwnedArray<juce::PluginDescription> descriptions;
        format->findAllTypesForFile(descriptions, bundle.getFullPathName());
        if (descriptions.isEmpty())
            return 66;

        // Metadata alone is insufficient: some bundles only fail while their
        // component is constructed. This helper is intentionally disposable.
        juce::String error;
        auto instance = formats.createPluginInstance(*descriptions[0], 48000.0, 512, error);
        if (instance == nullptr)
            return 69;

        // Metadata/instantiation alone does not exercise the VST3 audio-bus
        // mapper. Verify the exact stereo live-host contract before accepting
        // a bundle into StudioForge's registry.
        auto layout = instance->getBusesLayout();
        if (instance->getBusCount(true) > 0)
            layout.getMainInputChannelSet() = juce::AudioChannelSet::stereo();
        if (instance->getBusCount(false) > 0)
            layout.getMainOutputChannelSet() = juce::AudioChannelSet::stereo();
        if (! instance->setBusesLayout(layout))
            return 70;
        instance->enableAllBuses();
        instance->setPlayConfigDetails(2, 2, 48000.0, 512);
        instance->prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> testBuffer(2, 128);
        testBuffer.clear();
        juce::MidiBuffer midi;
        instance->processBlock(testBuffer, midi);
        instance->releaseResources();

        juce::XmlElement document { "KNOWNPLUGINS" };
        for (const auto* description : descriptions)
            if (description != nullptr)
                document.addChildElement(description->createXml().release());
        // writeTo() uses a second temporary file internally. This helper already
        // owns a private output path, so a direct atomic replacement avoids a
        // nested temporary-file failure in restricted/macOS launch contexts.
        return output.replaceWithText(document.toString()) ? 0 : 67;
    }

    return 68;
}
