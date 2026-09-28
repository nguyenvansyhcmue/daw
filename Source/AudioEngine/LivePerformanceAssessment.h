#pragma once

#include <cstdint>

enum class LivePerformanceRating
{
    ready,
    caution,
    notReady
};

struct LivePerformanceMeasurement
{
    double sampleRate = 0.0;
    int inputLatencySamples = 0;
    int outputLatencySamples = 0;
    int pluginLatencySamples = 0;
    float peakCallbackLoad = 0.0f;
    uint64_t overloadsDuringTest = 0;
};

struct LivePerformanceAssessment
{
    LivePerformanceRating rating = LivePerformanceRating::notReady;
    double roundTripMilliseconds = 0.0;
};

inline LivePerformanceAssessment assessLivePerformance(const LivePerformanceMeasurement& measurement) noexcept
{
    if (measurement.sampleRate <= 0.0)
        return {};

    const auto totalSamples = measurement.inputLatencySamples + measurement.outputLatencySamples
        + measurement.pluginLatencySamples;
    const auto roundTripMilliseconds = 1000.0 * static_cast<double>(totalSamples) / measurement.sampleRate;
    const auto unstable = measurement.overloadsDuringTest > 0 || measurement.peakCallbackLoad >= 0.90f;
    const auto caution = measurement.peakCallbackLoad >= 0.70f || roundTripMilliseconds > 20.0;
    return { unstable ? LivePerformanceRating::notReady
                      : caution ? LivePerformanceRating::caution : LivePerformanceRating::ready,
             roundTripMilliseconds };
}
