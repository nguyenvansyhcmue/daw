#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

class TrackDataModel;
struct ClipId;

struct MediaReloadReport
{
    int decodedClipCount = 0;
    juce::StringArray missingMediaReferences;
    juce::StringArray decodeFailures;
};

class MediaReloadService final
{
public:
    static juce::Result decode(const juce::File& file, std::shared_ptr<juce::AudioBuffer<float>>& decodedBuffer);
    static MediaReloadReport reloadProjectMedia(TrackDataModel& model);
    static juce::Result relinkClip(TrackDataModel& model, ClipId clipId, const juce::File& replacementFile);
};
