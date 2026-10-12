#include "ControlRoutingProfile.h"

const ControlRoute* ControlRoutingProfile::find(ControlTarget target, ControlEffect effect) const noexcept
{
    for (const auto& route : routes)
        if (route.target == target && route.effect == effect)
            return &route;
    return nullptr;
}

juce::Result ControlRoutingProfile::validate() const
{
    for (const auto& route : routes)
    {
        if (! route.track.isValid() || route.fxSlot < 0 || route.parameterIdentifier.trim().isEmpty())
            return juce::Result::fail("A control route needs a track, FX slot, and parameter identifier");
        if (route.effect == ControlEffect::pitchCorrection && route.secondaryParameterIdentifier.trim().isEmpty())
            return juce::Result::fail("Pitch-correction routes need key and scale parameter identifiers");
    }
    return juce::Result::ok();
}
