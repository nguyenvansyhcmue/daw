#pragma once

#include <juce_core/juce_core.h>

#include "ProjectIdentifiers.h"

#include <vector>

// A take folder owns recording alternatives non-destructively.  Audio media
// remains in AudioClipState; the folder only describes which take contributes
// to the currently selected comp at each timeline interval.
struct CompSegmentState
{
    double startSample = 0.0;
    double durationSamples = 0.0;
    uint64_t sourceTakeId = 0;
    double fadeInSamples = 0.0;
    double fadeOutSamples = 0.0;
};

struct TakeCompState
{
    CompId id;
    juce::String name;
    std::vector<CompSegmentState> segments;
};

struct TakeFolderState
{
    TakeFolderId id;
    TrackId trackId;
    double startSample = 0.0;
    double durationSamples = 0.0;
    std::vector<uint64_t> takeIds;
    std::vector<TakeCompState> comps;
    CompId activeCompId;
    bool quickSwipeEnabled = true;
};
