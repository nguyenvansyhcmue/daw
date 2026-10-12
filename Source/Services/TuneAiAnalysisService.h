#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include "../Control/MusicalKey.h"

class TuneAiAnalysisService final
{
public:
    struct Result
    {
        bool succeeded = false;
        bool fromCache = false;
        MusicalKey key;
        float confidence = 0.0f;
        juce::String title;
        juce::String error;
    };

    using Completion = std::function<void(Result)>;

    TuneAiAnalysisService() = default;
    ~TuneAiAnalysisService();

    void analyseYouTubeUrlAsync(juce::String url, Completion completion);
    juce::File findBridgeScript() const;
    juce::File findStandaloneAppScript() const;
    // The packaged app is a separate process. This rectangle lets it present
    // borderlessly over StudioForge without claiming it is a JUCE child view.
    juce::Result launchStandaloneApp(juce::Rectangle<int> hostBounds = {}) const;

private:
    juce::ThreadPool workers { 1 };
};
