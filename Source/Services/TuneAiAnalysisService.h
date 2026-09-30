#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

class TuneAiAnalysisService final
{
public:
    struct Result
    {
        bool succeeded = false;
        bool fromCache = false;
        int rootNote = 0;
        bool minor = false;
        float confidence = 0.0f;
        juce::String title;
        juce::String error;
    };

    using Completion = std::function<void(Result)>;

    TuneAiAnalysisService() = default;
    ~TuneAiAnalysisService();

    void analyseYouTubeUrlAsync(juce::String url, Completion completion);
    juce::File findBridgeScript() const;

private:
    juce::ThreadPool workers { 1 };
};
