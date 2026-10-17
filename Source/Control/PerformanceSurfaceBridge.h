#pragma once

#include "PerformanceSurfaceProtocol.h"

// A local-only transport for the external Tune-AI performance surface. On
// macOS/Linux it uses a Unix-domain socket; it never binds a TCP port and it
// never carries audio. Protocol validation remains in PerformanceSurfaceProtocol.
class PerformanceSurfaceBridge final : private juce::Thread
{
public:
    PerformanceSurfaceBridge(TrackDataModel& model, PerformanceMacroService& macros);
    ~PerformanceSurfaceBridge() override;

    juce::Result start();
    void stop();
    juce::File getConfigurationFile() const;

private:
    void run() override;
    juce::String dispatchOnMessageThread(const juce::String& request);
    juce::Result writeConfiguration() const;

    TrackDataModel& dataModel;
    PerformanceMacroService& macros;
    juce::String sessionToken;
    juce::String socketPath;
    int listeningSocket = -1;
    std::atomic<bool> accepting { false };
};
