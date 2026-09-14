#pragma once

#include <juce_core/juce_core.h>

class TrackDataModel;

class ProjectSerializer final
{
public:
    static juce::Result save(const TrackDataModel& model, const juce::File& file);
    static juce::Result load(TrackDataModel& model, const juce::File& file,
                             juce::StringArray* missingMediaReferences = nullptr);
};
