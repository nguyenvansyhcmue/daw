#pragma once

#include <juce_core/juce_core.h>

#include <vector>

class StudioForgeCalibrationRunner final
{
public:
    struct Settings
    {
        float retuneMs = 10.0f;
        float flexTune = 0.0f;
        float humanize = 0.0f;
        float naturalVibrato = 0.0f;
    };

    struct Request
    {
        juce::File input;
        juce::File outputDirectory;
        juce::String pluginQuery { "VibeAutotune" };
        // A self-built VST3 can be validated before it is installed into a
        // system plug-in location or registered as an Audio Unit.
        juce::File pluginPath;
        double sampleRate = 48000.0;
        int blockSize = 256;
        std::vector<Settings> cases;
    };

    // Renders every case through StudioForge's production plug-in host and
    // offline bounce graph. `report` is CSV so CI can archive it directly.
    static juce::Result run(const Request& request, juce::String& report);
};
