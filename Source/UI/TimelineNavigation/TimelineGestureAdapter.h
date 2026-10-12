#pragma once

#include <cmath>

enum class TimelineWheelIntent { panHorizontal, panVertical, zoomHorizontal };

struct TimelineWheelGesture final
{
    bool commandOrControl = false;
    bool shift = false;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
};

inline TimelineWheelIntent timelineWheelIntent(const TimelineWheelGesture& gesture) noexcept
{
    if (gesture.commandOrControl) return TimelineWheelIntent::zoomHorizontal;
    return gesture.shift || std::abs(gesture.deltaX) > std::abs(gesture.deltaY)
        ? TimelineWheelIntent::panHorizontal : TimelineWheelIntent::panVertical;
}
