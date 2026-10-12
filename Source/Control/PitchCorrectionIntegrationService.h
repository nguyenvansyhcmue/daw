#pragma once

#include "StudioForgeControlBridge.h"
#include "VibeAutotuneAdapter.h"
#include "../Plugins/PluginHostService.h"

#include <optional>

enum class PitchCorrectionIntegrationState : uint8_t
{
    ready, pluginNotScanned, pluginNotInstalled, pluginNotInserted, routeInvalid, parameterMappingInvalid, wrongPluginInSlot
};

class PitchCorrectionIntegrationService final
{
public:
    struct Status
    {
        PitchCorrectionIntegrationState state = PitchCorrectionIntegrationState::pluginNotScanned;
        juce::String message;
        juce::String pluginIdentifier;
        juce::String format;
        juce::String rootParameter;
        juce::String scaleParameter;
        bool ready() const noexcept { return state == PitchCorrectionIntegrationState::ready; }
    };

    PitchCorrectionIntegrationService(TrackDataModel&, PluginHostService&, StudioForgeControlBridge&);
    Status validate() const;
    Status ensureVibeAutotuneForVocalLive();
    StudioForgeControlBridge::Result applyKey(ControlTarget, const MusicalKey&);

private:
    static bool isVibeAutotune(const juce::PluginDescription&) noexcept;
    std::optional<juce::PluginDescription> findVibeAutotune() const;
    Status validateRoute(const ControlRoute&, const juce::PluginDescription*) const;

    TrackDataModel& model;
    PluginHostService& host;
    StudioForgeControlBridge& bridge;
};
