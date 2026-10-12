#include "TimelineViewportController.h"

TimelineViewportController::TimelineViewportController(TimelineViewportState initial) : current(initial)
{
    history[0] = initial;
    historySize = 1;
    historyIndex = 0;
}

void TimelineViewportController::setState(TimelineViewportState next, bool recordHistory)
{
    if (current.approximatelyEquals(next)) return;
    current = next;
    if (recordHistory)
    {
        if (historyIndex + 1 < historySize) historySize = historyIndex + 1;
        if (historySize == static_cast<int>(history.size()))
        {
            for (int i = 1; i < historySize; ++i) history[i - 1] = history[i];
            --historySize;
        }
        history[historySize++] = current;
        historyIndex = historySize - 1;
    }
    notify();
}
bool TimelineViewportController::back()
{
    if (historyIndex <= 0) return false;
    current = history[--historyIndex]; notify(); return true;
}
bool TimelineViewportController::forward()
{
    if (historyIndex + 1 >= historySize) return false;
    current = history[++historyIndex]; notify(); return true;
}
void TimelineViewportController::notify() { if (onChanged != nullptr) onChanged(current); }
