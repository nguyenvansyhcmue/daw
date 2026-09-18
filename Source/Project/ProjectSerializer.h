#pragma once

#include <juce_core/juce_core.h>

class TrackDataModel;
class PluginHostService;

class ProjectSerializer final
{
public:
    static juce::Result save(const TrackDataModel& model, const juce::File& file);
    // Writes only the session document. Recovery files keep references to the
    // existing media instead of duplicating it beside every autosave.
    static juce::Result saveRecoverySnapshot(const TrackDataModel& model, const juce::File& file);
    static juce::Result load(TrackDataModel& model, const juce::File& file,
                             juce::StringArray* missingMediaReferences = nullptr);
    static juce::Result load(TrackDataModel& model, PluginHostService& pluginHost, const juce::File& file,
                             juce::StringArray* missingReferences = nullptr);
};
