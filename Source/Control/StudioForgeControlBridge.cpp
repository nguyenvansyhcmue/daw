#include "StudioForgeControlBridge.h"
#include "MusicalKey.h"
#include "VibeAutotuneAdapter.h"

#include "../Models/TrackDataModel.h"

namespace
{
juce::String targetName(ControlTarget target)
{
    switch (target)
    {
        case ControlTarget::mic1: return "mic1";
        case ControlTarget::mic2: return "mic2";
        case ControlTarget::music: return "music";
        case ControlTarget::master: return "master";
    }
    return "unknown";
}

juce::String noteName(int root)
{
    static constexpr const char* notes[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return notes[static_cast<size_t>(root)];
}
}

StudioForgeControlBridge::Result StudioForgeControlBridge::apply(const ControlCommand& command)
{
    Result result;
    if (const auto validation = command.validate(); validation.failed())
    {
        result.error = validation.getErrorMessage();
        return result;
    }
    const auto& profile = dataModel.getControlRoutingProfile();
    if (const auto validation = profile.validate(); validation.failed())
    {
        result.error = "Control routing profile is invalid: " + validation.getErrorMessage();
        return result;
    }
    const auto* route = profile.find(command.target, command.effect);
    if (route == nullptr)
    {
        result.error = "No control route is configured for " + targetName(command.target);
        return result;
    }
    const auto trackIndex = dataModel.getTrackIndex(route->track);
    if (trackIndex < 0)
    {
        result.error = "Configured control track is missing for " + targetName(command.target);
        return result;
    }
    const auto* rack = dataModel.getFxRackSnapshot(static_cast<size_t>(trackIndex));
    if (rack == nullptr || route->fxSlot >= static_cast<int>(rack->processors.size())
        || rack->processors[static_cast<size_t>(route->fxSlot)] == nullptr)
    {
        result.error = "Configured FX slot is empty for " + targetName(command.target);
        return result;
    }
    const auto& processor = rack->processors[static_cast<size_t>(route->fxSlot)];
    if (route->expectedPluginIdentifier.isNotEmpty()
        && processor->getPersistentIdentifier() != route->expectedPluginIdentifier)
    {
        result.error = "Configured plug-in does not match the route for " + targetName(command.target);
        return result;
    }

    bool applied = false;
    if (command.type == ControlCommandType::setKeySignature)
    {
        const auto& key = *command.keySignature;
        const MusicalKey musicalKey { static_cast<PitchClass>(key.rootNote), key.minor ? MusicalScale::minor : MusicalScale::major };
        const auto adapterResult = VibeAutotuneAdapter {}.apply(*processor, musicalKey,
                                                                route->parameterIdentifier, route->secondaryParameterIdentifier);
        applied = adapterResult.succeeded;
        result.resolvedParameter = adapterResult.mapping.root + ", " + adapterResult.mapping.scale;
        if (! applied) result.error = adapterResult.error;
    }
    else if (command.type == ControlCommandType::setBypass)
    {
        dataModel.setFxBypassed(static_cast<size_t>(trackIndex), static_cast<size_t>(route->fxSlot), command.bypassed);
        applied = true;
        result.resolvedParameter = "bypass";
    }
    else
    {
        applied = processor->setParameterValue(route->parameterIdentifier, command.value);
        result.resolvedParameter = route->parameterIdentifier;
    }
    if (! applied)
    {
        if (result.error.isEmpty()) result.error = "Plug-in does not expose the configured parameter(s): " + result.resolvedParameter;
        return result;
    }
    result.succeeded = true;
    result.appliedTargets.add(targetName(command.target));
    result.resolvedPlugin = processor->getPersistentIdentifier().isNotEmpty() ? processor->getPersistentIdentifier() : processor->getName();
    return result;
}

void StudioForgeControlBridge::post(ControlCommand command, std::function<void(Result)> completion)
{
    juce::MessageManager::callAsync([this, command = std::move(command), completion = std::move(completion)]() mutable
    {
        auto result = apply(command);
        if (completion) completion(std::move(result));
    });
}

juce::StringArray StudioForgeControlBridge::validateProfile() const
{
    juce::StringArray errors;
    const auto& profile = dataModel.getControlRoutingProfile();
    if (const auto validation = profile.validate(); validation.failed())
        errors.add(validation.getErrorMessage());
    for (const auto& route : profile.routes)
    {
        const auto index = dataModel.getTrackIndex(route.track);
        if (index < 0) { errors.add("Control route references a missing track"); continue; }
        const auto* rack = dataModel.getFxRackSnapshot(static_cast<size_t>(index));
        if (rack == nullptr || route.fxSlot >= static_cast<int>(rack->processors.size())
            || rack->processors[static_cast<size_t>(route.fxSlot)] == nullptr)
            errors.add("Control route references an empty FX slot");
    }
    return errors;
}
