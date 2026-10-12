#pragma once

#include "TimelineViewportState.h"

#include <array>
#include <functional>

class TimelineViewportController final
{
public:
    explicit TimelineViewportController(TimelineViewportState initial = {});
    [[nodiscard]] const TimelineViewportState& state() const noexcept { return current; }
    void setState(TimelineViewportState next, bool recordHistory = true);
    bool back();
    bool forward();
    std::function<void(const TimelineViewportState&)> onChanged;
private:
    void notify();
    std::array<TimelineViewportState, 20> history {};
    int historySize = 0, historyIndex = -1;
    TimelineViewportState current;
};
