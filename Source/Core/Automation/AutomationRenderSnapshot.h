#pragma once

#include "AutomationLane.h"

#include <algorithm>
#include <array>
#include <cmath>

struct AutomationRenderLane
{
    AutomationParameter parameter = AutomationParameter::trackVolume;
    AutomationMode mode = AutomationMode::read;
    float defaultValue = 1.0f;
    uint16_t pointCount = 0;
    std::array<AutomationPoint, AutomationLane::maximumPoints> points {};

    float evaluate(double samplePosition) const noexcept
    {
        if (pointCount == 0 || ! std::isfinite(samplePosition))
            return defaultValue;
        if (samplePosition <= points[0].samplePosition)
            return points[0].value;
        if (samplePosition >= points[pointCount - 1].samplePosition)
            return points[pointCount - 1].value;

        const auto end = points.begin() + pointCount;
        const auto upper = std::upper_bound(points.begin(), end, samplePosition,
                                            [] (double position, const AutomationPoint& point)
                                            {
                                                return position < point.samplePosition;
                                            });
        const auto& left = *(upper - 1);
        const auto& right = *upper;
        const auto fraction = static_cast<float>((samplePosition - left.samplePosition)
                                                 / (right.samplePosition - left.samplePosition));
        return left.value + (right.value - left.value) * fraction;
    }

    static AutomationRenderLane compile(const AutomationLane& source) noexcept
    {
        AutomationRenderLane snapshot;
        snapshot.parameter = source.getParameter();
        snapshot.mode = source.getMode();
        snapshot.defaultValue = source.getDefaultValue();
        const auto& sourcePoints = source.getPoints();
        snapshot.pointCount = static_cast<uint16_t>(sourcePoints.size());
        std::copy(sourcePoints.begin(), sourcePoints.end(), snapshot.points.begin());
        return snapshot;
    }
};
