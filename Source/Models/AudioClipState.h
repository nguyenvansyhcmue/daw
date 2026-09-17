#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

#include "ProjectIdentifiers.h"

enum class AudioMediaStatus : uint8_t
{
    Unloaded,
    Ready,
    Missing,
    DecodeFailed
};

struct AudioClipState
{
    ClipId id;
    AudioSourceId sourceId;
    juce::File sourceFile;
    double startSample { 0.0 };
    double durationSamples { 0.0 };
    double sourceOffsetSamples { 0.0 };
    float gain { 1.0f };
    double fadeInSamples { 0.0 };
    double fadeOutSamples { 0.0 };
    TrackId trackId;
    juce::String clipName;
    std::shared_ptr<juce::AudioBuffer<float>> cachedBuffer;
    AudioMediaStatus mediaStatus { AudioMediaStatus::Unloaded };
};
