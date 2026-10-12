#pragma once

#include <cmath>

struct TimelineViewportState final
{
    double viewStartSample = 0.0;
    float horizontalZoom = 1.0f;
    int trackHeight = 72;
    float waveformZoom = 1.0f;

    [[nodiscard]] bool approximatelyEquals(const TimelineViewportState& other) const noexcept
    {
        return std::abs(viewStartSample - other.viewStartSample) < 0.01
            && std::abs(horizontalZoom - other.horizontalZoom) < 0.0001f
            && trackHeight == other.trackHeight
            && std::abs(waveformZoom - other.waveformZoom) < 0.0001f;
    }
};
