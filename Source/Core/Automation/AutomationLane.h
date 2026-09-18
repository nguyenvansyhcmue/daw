#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

enum class AutomationParameter : uint8_t
{
    trackVolume,
    trackPan,
    sendLevel
};

enum class AutomationMode : uint8_t
{
    read,
    touch,
    latch,
    write
};

struct AutomationPoint
{
    double samplePosition = 0.0;
    float value = 0.0f;
};

class AutomationLane final
{
public:
    static constexpr size_t maximumPoints = 2048;

    explicit AutomationLane(AutomationParameter parameter, float defaultValue);

    AutomationParameter getParameter() const noexcept;
    AutomationMode getMode() const noexcept;
    void setMode(AutomationMode newMode) noexcept;

    float getDefaultValue() const noexcept;
    const std::vector<AutomationPoint>& getPoints() const noexcept;

    void upsertPoint(double samplePosition, float value);
    bool removePoint(double samplePosition) noexcept;
    void clear() noexcept;
    float evaluate(double samplePosition) const noexcept;

    static bool isValueValid(AutomationParameter parameter, float value) noexcept;

private:
    AutomationParameter parameter;
    AutomationMode mode = AutomationMode::read;
    float defaultValue = 0.0f;
    std::vector<AutomationPoint> points;
};
