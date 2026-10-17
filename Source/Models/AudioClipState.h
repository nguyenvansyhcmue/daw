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
    // A looped region repeats this source window while its timeline length is
    // extended. Zero means "derive from available source" for old sessions.
    bool loopEnabled { false };
    double loopLengthSamples { 0.0 };
    float gain { 1.0f };
    double fadeInSamples { 0.0 };
    double fadeOutSamples { 0.0 };
    // Recorded passes are kept together without destroying an earlier take.
    // A zero group means a normal timeline clip.  Within a non-zero group the
    // engine renders only the active take.
    uint64_t takeGroupId { 0 };
    // A take can be split into multiple arrangement regions.  This stable
    // identity keeps those fragments acting as one selectable pass.
    uint64_t takeId { 0 };
    bool takeActive { true };
    bool recordedTake { false };
    TrackId trackId;
    juce::String clipName;
    std::shared_ptr<juce::AudioBuffer<float>> cachedBuffer;
    AudioMediaStatus mediaStatus { AudioMediaStatus::Unloaded };
};
