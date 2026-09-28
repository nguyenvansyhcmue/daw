#include "AudioEngine/LivePerformanceAssessment.h"

#include <cmath>
#include <iostream>

namespace
{
bool expect(bool condition, const char* message)
{
    if (! condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main()
{
    const auto ready = assessLivePerformance({ 48000.0, 96, 96, 0, 0.42f, 0 });
    const auto highLatency = assessLivePerformance({ 48000.0, 512, 512, 0, 0.42f, 0 });
    const auto highLoad = assessLivePerformance({ 48000.0, 96, 96, 0, 0.75f, 0 });
    const auto overloaded = assessLivePerformance({ 48000.0, 96, 96, 0, 0.91f, 1 });
    const auto noDevice = assessLivePerformance({});

    const auto passed = expect(ready.rating == LivePerformanceRating::ready
                                   && std::abs(ready.roundTripMilliseconds - 4.0) < 1.0e-6,
                               "a stable low-latency path passes")
        && expect(highLatency.rating == LivePerformanceRating::caution,
                  "high reported latency requires caution")
        && expect(highLoad.rating == LivePerformanceRating::caution,
                  "high callback load requires caution")
        && expect(overloaded.rating == LivePerformanceRating::notReady,
                  "a callback overload fails live readiness")
        && expect(noDevice.rating == LivePerformanceRating::notReady,
                  "a missing device fails live readiness");
    return passed ? 0 : 1;
}
