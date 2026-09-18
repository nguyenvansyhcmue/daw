#pragma once

#include "Models/TrackDataModel.h"

enum class InspectorRegionKind
{
    none,
    audio,
    midi
};

struct InspectorRegionViewState
{
    InspectorRegionKind kind = InspectorRegionKind::none;
    juce::String title;
    juce::String timelineDetails;
    juce::String modifierDetails;
};

struct InspectorTrackViewState
{
    bool isSelected = false;
    bool supportsInputMonitoring = false;
    juce::String name;
    juce::String inputDetails;
    juce::String outputDetails;
};

class InspectorViewStateBuilder final
{
public:
    static InspectorRegionViewState makeEmptyRegion();
    static InspectorRegionViewState makeAudioRegion(const TrackDataModel&, ClipId);
    static InspectorRegionViewState makeMidiRegion(const TrackDataModel&, MidiClipId);
    static InspectorTrackViewState makeTrack(const TrackDataModel&, int trackIndex);
};
