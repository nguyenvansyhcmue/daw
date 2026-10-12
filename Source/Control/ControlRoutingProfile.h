#pragma once

#include "ControlCommand.h"
#include "../Models/ProjectIdentifiers.h"

#include <vector>

struct ControlRoute
{
    ControlTarget target = ControlTarget::mic1;
    ControlEffect effect = ControlEffect::pitchCorrection;
    TrackId track;
    int fxSlot = -1;
    juce::String expectedPluginIdentifier;
    juce::String parameterIdentifier;
    juce::String secondaryParameterIdentifier;
};

struct ControlRoutingProfile
{
    std::vector<ControlRoute> routes;
    const ControlRoute* find(ControlTarget target, ControlEffect effect) const noexcept;
    juce::Result validate() const;
};
