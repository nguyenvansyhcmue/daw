#include "AutomationLane.h"

#include "../AudioGainRange.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
bool isFinitePosition(double samplePosition) noexcept
{
    return std::isfinite(samplePosition) && samplePosition >= 0.0;
}
}

AutomationLane::AutomationLane(AutomationParameter parameterToUse, float defaultValueToUse)
    : parameter(parameterToUse), defaultValue(defaultValueToUse)
{
    if (! isValueValid(parameter, defaultValue))
        throw std::invalid_argument("Automation default value is outside the parameter range");
}

AutomationParameter AutomationLane::getParameter() const noexcept
{
    return parameter;
}

AutomationMode AutomationLane::getMode() const noexcept
{
    return mode;
}

void AutomationLane::setMode(AutomationMode newMode) noexcept
{
    mode = newMode;
}

float AutomationLane::getDefaultValue() const noexcept
{
    return defaultValue;
}

const std::vector<AutomationPoint>& AutomationLane::getPoints() const noexcept
{
    return points;
}

void AutomationLane::upsertPoint(double samplePosition, float value)
{
    if (! isFinitePosition(samplePosition))
        throw std::invalid_argument("Automation sample position must be finite and non-negative");
    if (! isValueValid(parameter, value))
        throw std::invalid_argument("Automation value is outside the parameter range");

    const auto insertion = std::lower_bound(points.begin(), points.end(), samplePosition,
                                            [] (const AutomationPoint& point, double position)
                                            {
                                                return point.samplePosition < position;
                                            });
    if (insertion != points.end() && insertion->samplePosition == samplePosition)
    {
        insertion->value = value;
        return;
    }
    if (points.size() == maximumPoints)
        throw std::length_error("Automation lane reached its fixed point capacity");

    points.insert(insertion, { samplePosition, value });
}

bool AutomationLane::removePoint(double samplePosition) noexcept
{
    const auto found = std::lower_bound(points.begin(), points.end(), samplePosition,
                                        [] (const AutomationPoint& point, double position)
                                        {
                                            return point.samplePosition < position;
                                        });
    if (found == points.end() || found->samplePosition != samplePosition)
        return false;

    points.erase(found);
    return true;
}

void AutomationLane::clear() noexcept
{
    points.clear();
}

float AutomationLane::evaluate(double samplePosition) const noexcept
{
    if (points.empty() || ! std::isfinite(samplePosition))
        return defaultValue;
    if (samplePosition <= points.front().samplePosition)
        return points.front().value;
    if (samplePosition >= points.back().samplePosition)
        return points.back().value;

    const auto upper = std::upper_bound(points.begin(), points.end(), samplePosition,
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

bool AutomationLane::isValueValid(AutomationParameter parameter, float value) noexcept
{
    if (! std::isfinite(value))
        return false;

    switch (parameter)
    {
        case AutomationParameter::trackVolume: return value >= 0.0f && value <= 2.0f;
        case AutomationParameter::trackPan:    return value >= -1.0f && value <= 1.0f;
        case AutomationParameter::sendLevel:   return value >= 0.0f && value <= AudioGainRange::maximumSendGain;
    }

    return false;
}
