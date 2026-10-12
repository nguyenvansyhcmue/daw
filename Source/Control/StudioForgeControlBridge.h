#pragma once

#include "ControlCommand.h"
#include "ControlRoutingProfile.h"

#include <functional>

class TrackDataModel;

class StudioForgeControlBridge final
{
public:
    struct Result
    {
        bool succeeded = false;
        juce::StringArray appliedTargets;
        juce::String resolvedPlugin;
        juce::String resolvedParameter;
        juce::String warning;
        juce::String error;
    };

    explicit StudioForgeControlBridge(TrackDataModel& model) : dataModel(model) {}

    // Call on the message/control thread. This path never executes in the audio callback.
    Result apply(const ControlCommand& command);
    void post(ControlCommand command, std::function<void(Result)> completion);
    juce::StringArray validateProfile() const;

private:
    TrackDataModel& dataModel;
};
