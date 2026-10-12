#include "PitchCorrectionIntegrationService.h"

#include "../Models/TrackDataModel.h"

namespace
{
juce::String normalized(juce::String text)
{
    return text.toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789");
}

juce::String stateMessage(PitchCorrectionIntegrationState state)
{
    switch (state)
    {
        case PitchCorrectionIntegrationState::ready: return "VibeAutotune is ready";
        case PitchCorrectionIntegrationState::pluginNotScanned: return "Plug-in registry is empty. Scan plug-ins first.";
        case PitchCorrectionIntegrationState::pluginNotInstalled: return "VibeAutotune was not found in the plug-in registry.";
        case PitchCorrectionIntegrationState::pluginNotInserted: return "VibeAutotune is not inserted on the configured vocal route.";
        case PitchCorrectionIntegrationState::routeInvalid: return "Vocal pitch-correction route is invalid.";
        case PitchCorrectionIntegrationState::parameterMappingInvalid: return "VibeAutotune root/scale parameter mapping is invalid.";
        case PitchCorrectionIntegrationState::wrongPluginInSlot: return "Configured vocal FX slot contains another plug-in.";
    }
    return {};
}
}

PitchCorrectionIntegrationService::PitchCorrectionIntegrationService(TrackDataModel& modelIn, PluginHostService& hostIn,
                                                                     StudioForgeControlBridge& bridgeIn)
    : model(modelIn), host(hostIn), bridge(bridgeIn) {}

bool PitchCorrectionIntegrationService::isVibeAutotune(const juce::PluginDescription& description) noexcept
{
    // Persistent identifiers survive display-name changes; exact normalized
    // comparisons avoid accidentally accepting another plug-in named "Vibe".
    const auto identifier = normalized(description.createIdentifierString());
    const auto name = normalized(description.name);
    return identifier == "vibeautotune" || identifier.endsWith("vibeautotune") || name == "vibeautotune";
}

std::optional<juce::PluginDescription> PitchCorrectionIntegrationService::findVibeAutotune() const
{
    const auto plugins = host.getKnownPlugins();
    std::optional<juce::PluginDescription> fallback;
    for (const auto& plugin : plugins)
    {
        if (! isVibeAutotune(plugin))
            continue;
        if (plugin.pluginFormatName.equalsIgnoreCase("VST3")
            && juce::File { plugin.fileOrIdentifier }.exists())
            return plugin;
        if (! fallback.has_value())
            fallback = plugin;
    }
    // AU remains a compatibility fallback for Logic, but StudioForge always
    // chooses the one canonical VST3 when it is installed.
    return fallback;
}

PitchCorrectionIntegrationService::Status PitchCorrectionIntegrationService::validateRoute(const ControlRoute& route,
                                                                                             const juce::PluginDescription* installed) const
{
    if (! route.track.isValid() || route.fxSlot < 0 || route.fxSlot >= static_cast<int>(TrackDataModel::maxFxSlots))
        return { PitchCorrectionIntegrationState::routeInvalid, stateMessage(PitchCorrectionIntegrationState::routeInvalid) };
    const auto track = model.getTrackIndex(route.track);
    if (track < 0) return { PitchCorrectionIntegrationState::routeInvalid, "Configured vocal track is missing." };
    const auto* rack = model.getFxRackSnapshot(static_cast<size_t>(track));
    const auto* processor = rack != nullptr ? rack->processors[static_cast<size_t>(route.fxSlot)].get() : nullptr;
    if (processor == nullptr) return { PitchCorrectionIntegrationState::pluginNotInserted, stateMessage(PitchCorrectionIntegrationState::pluginNotInserted) };
    if (installed != nullptr && processor->getPersistentIdentifier() != installed->createIdentifierString())
        return { PitchCorrectionIntegrationState::wrongPluginInSlot, stateMessage(PitchCorrectionIntegrationState::wrongPluginInSlot) };
    if (route.expectedPluginIdentifier.isNotEmpty() && processor->getPersistentIdentifier() != route.expectedPluginIdentifier)
        return { PitchCorrectionIntegrationState::wrongPluginInSlot, stateMessage(PitchCorrectionIntegrationState::wrongPluginInSlot) };
    const auto mapping = VibeAutotuneAdapter {}.resolve(*processor, route.parameterIdentifier, route.secondaryParameterIdentifier);
    if (! mapping.succeeded)
        return { PitchCorrectionIntegrationState::parameterMappingInvalid, mapping.error };
    return { PitchCorrectionIntegrationState::ready, stateMessage(PitchCorrectionIntegrationState::ready),
             processor->getPersistentIdentifier(), installed != nullptr ? installed->pluginFormatName : juce::String {},
             mapping.mapping.root, mapping.mapping.scale };
}

PitchCorrectionIntegrationService::Status PitchCorrectionIntegrationService::validate() const
{
    const auto plugins = host.getKnownPlugins();
    if (plugins.isEmpty()) return { PitchCorrectionIntegrationState::pluginNotScanned, stateMessage(PitchCorrectionIntegrationState::pluginNotScanned) };
    const auto vibe = findVibeAutotune();
    if (! vibe.has_value()) return { PitchCorrectionIntegrationState::pluginNotInstalled, stateMessage(PitchCorrectionIntegrationState::pluginNotInstalled) };
    const auto& profile = model.getControlRoutingProfile();
    auto routeCount = 0;
    for (const auto& route : profile.routes)
    {
        if (route.effect != ControlEffect::pitchCorrection
            || (route.target != ControlTarget::mic1 && route.target != ControlTarget::mic2))
            continue;
        ++routeCount;
        if (const auto status = validateRoute(route, &*vibe); ! status.ready()) return status;
    }
    if (routeCount == 0)
        return { PitchCorrectionIntegrationState::routeInvalid, "No vocal pitch-correction route is configured." };
    return { PitchCorrectionIntegrationState::ready, stateMessage(PitchCorrectionIntegrationState::ready), vibe->createIdentifierString(), vibe->pluginFormatName };
}

PitchCorrectionIntegrationService::Status PitchCorrectionIntegrationService::ensureVibeAutotuneForVocalLive()
{
    const auto plugins = host.getKnownPlugins();
    if (plugins.isEmpty()) return { PitchCorrectionIntegrationState::pluginNotScanned, stateMessage(PitchCorrectionIntegrationState::pluginNotScanned) };
    const auto vibe = findVibeAutotune();
    if (! vibe.has_value()) return { PitchCorrectionIntegrationState::pluginNotInstalled, stateMessage(PitchCorrectionIntegrationState::pluginNotInstalled) };

    auto profile = model.getControlRoutingProfile();
    auto routeCount = 0;
    for (auto& candidate : profile.routes)
    {
        if (candidate.effect != ControlEffect::pitchCorrection
            || (candidate.target != ControlTarget::mic1 && candidate.target != ControlTarget::mic2))
            continue;
        ++routeCount;
        auto* route = &candidate;
        const auto track = model.getTrackIndex(route->track);
        if (track < 0) return { PitchCorrectionIntegrationState::routeInvalid, "Configured vocal track is missing." };
        const auto* rack = model.getFxRackSnapshot(static_cast<size_t>(track));
        auto existing = rack != nullptr ? rack->processors[static_cast<size_t>(route->fxSlot)] : nullptr;
        if (existing != nullptr
            && existing->getPersistentIdentifier() != vibe->createIdentifierString())
        {
            if (normalized(existing->getName()) != "vibeautotune")
                return { PitchCorrectionIntegrationState::wrongPluginInSlot, stateMessage(PitchCorrectionIntegrationState::wrongPluginInSlot) };

            // Keep the explicit Auto Key -> Vibe route on the canonical
            // scanned format while preserving the user's plug-in state. This
            // is product integration policy, not a private audio-host path.
            juce::MemoryBlock previousState;
            existing->getState(previousState);
            juce::String error;
            auto replacement = host.createEffect(*vibe, juce::jmax(44100.0, model.getSampleRate()), 512, error);
            if (replacement == nullptr)
                return { PitchCorrectionIntegrationState::pluginNotInserted, error };
            if (! previousState.isEmpty())
                replacement->setState(previousState.getData(), previousState.getSize());
            model.setFxProcessor(static_cast<size_t>(track), static_cast<size_t>(route->fxSlot), std::move(replacement));
            rack = model.getFxRackSnapshot(static_cast<size_t>(track));
            existing = rack->processors[static_cast<size_t>(route->fxSlot)];
        }
        if (existing == nullptr)
        {
            juce::String error;
            auto effect = host.createEffect(*vibe, juce::jmax(44100.0, model.getSampleRate()), 512, error);
            if (effect == nullptr) return { PitchCorrectionIntegrationState::pluginNotInserted, error };
            model.setFxProcessor(static_cast<size_t>(track), static_cast<size_t>(route->fxSlot), std::move(effect));
            rack = model.getFxRackSnapshot(static_cast<size_t>(track));
            existing = rack->processors[static_cast<size_t>(route->fxSlot)];
        }
        const auto mapping = VibeAutotuneAdapter {}.resolve(*existing, route->parameterIdentifier, route->secondaryParameterIdentifier);
        if (! mapping.succeeded) return { PitchCorrectionIntegrationState::parameterMappingInvalid, mapping.error };
        route->expectedPluginIdentifier = vibe->createIdentifierString();
        route->parameterIdentifier = mapping.mapping.root;
        route->secondaryParameterIdentifier = mapping.mapping.scale;
    }
    if (routeCount == 0)
        return { PitchCorrectionIntegrationState::routeInvalid, "Create or configure a Vocal track before preparing VibeAutotune." };
    if (const auto result = model.setControlRoutingProfile(std::move(profile)); result.failed())
        return { PitchCorrectionIntegrationState::routeInvalid, result.getErrorMessage() };
    return validate();
}

StudioForgeControlBridge::Result PitchCorrectionIntegrationService::applyKey(ControlTarget target, const MusicalKey& key)
{
    const auto status = validate();
    if (! status.ready())
    {
        StudioForgeControlBridge::Result result;
        result.error = status.message;
        return result;
    }
    return bridge.apply(ControlCommand::keySignatureFor(target, key.rootNote(), key.isMinor()));
}
