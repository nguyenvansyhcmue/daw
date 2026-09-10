#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

struct AudioClipState
{
    juce::File sourceFile;
    double startSample { 0.0 };
    double durationSamples { 0.0 };
    double sourceOffsetSamples { 0.0 };
    int trackID { 0 };
    juce::String clipName;
    std::shared_ptr<juce::AudioBuffer<float>> cachedBuffer;
};
