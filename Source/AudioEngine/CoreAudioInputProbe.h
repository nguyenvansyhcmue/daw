#pragma once

#include <juce_core/juce_core.h>

// A diagnostic-only view of Core Audio's hardware input controls.  It is
// queried by the tracer's worker thread, never by the real-time callback.
class CoreAudioInputProbe
{
public:
    struct State
    {
        float gain = -1.0f;
        int muted = -1;
        bool gainAvailable = false;
    };

    void selectDevice(const juce::String& name) noexcept;
    State read() const noexcept;

private:
    std::atomic<uint32_t> deviceId { 0 };
};
