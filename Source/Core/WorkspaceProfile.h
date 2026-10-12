#pragma once

#include "../UI/WorkspaceMode.h"

// Profiles describe intent, not individual track state. A track is still
// audible from an input only when its own monitor/arm policy permits it.
enum class RoutingPolicy : uint8_t { studioTrackOnly, stageScenePrepared, streamBusesPrepared };
enum class MonitorPolicy : uint8_t { explicitTrack, stageScene, streamRoute };
enum class LatencyPolicy : uint8_t { preserveDevice, preferLowLatency, balancedLive };
enum class SafetyPolicy : uint8_t { studioSafe, stageGuarded, streamGuarded };

struct WorkspaceProfile
{
    WorkspaceMode mode = WorkspaceMode::studio;
    RoutingPolicy routing = RoutingPolicy::studioTrackOnly;
    MonitorPolicy monitoring = MonitorPolicy::explicitTrack;
    LatencyPolicy latency = LatencyPolicy::preserveDevice;
    SafetyPolicy safety = SafetyPolicy::studioSafe;

    bool permitsInputMonitoring() const noexcept { return mode != WorkspaceMode::studio; }
    bool protectsLivePluginLatency() const noexcept { return mode != WorkspaceMode::studio; }

    static WorkspaceProfile forMode(WorkspaceMode mode) noexcept
    {
        switch (mode)
        {
            case WorkspaceMode::stage:
                return { mode, RoutingPolicy::stageScenePrepared, MonitorPolicy::stageScene,
                         LatencyPolicy::preferLowLatency, SafetyPolicy::stageGuarded };
            case WorkspaceMode::stream:
                return { mode, RoutingPolicy::streamBusesPrepared, MonitorPolicy::streamRoute,
                         LatencyPolicy::balancedLive, SafetyPolicy::streamGuarded };
            case WorkspaceMode::studio:
            default:
                return {};
        }
    }
};
