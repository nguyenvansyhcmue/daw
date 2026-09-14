#pragma once

#include <algorithm>
#include <cmath>

class TransportUtils final
{
public:
    static bool hasValidCycle(bool active, double startSample, double endSample) noexcept
    {
        return active && endSample > startSample;
    }

    static double normalisePlayhead(double playhead, bool cycleActive,
                                    double cycleStart, double cycleEnd) noexcept
    {
        if (hasValidCycle(cycleActive, cycleStart, cycleEnd) && playhead >= cycleEnd)
            return cycleStart;
        return std::max(0.0, playhead);
    }

    static int samplesUntilBoundary(double playhead, int remainingSamples,
                                    bool cycleActive, double cycleStart,
                                    double cycleEnd) noexcept
    {
        if (remainingSamples <= 0 || ! hasValidCycle(cycleActive, cycleStart, cycleEnd))
            return remainingSamples;

        const auto samplesToEnd = static_cast<int>(std::ceil(cycleEnd - playhead));
        return std::clamp(samplesToEnd, 1, remainingSamples);
    }

    static double advance(double playhead, int samples, bool cycleActive,
                          double cycleStart, double cycleEnd) noexcept
    {
        const auto advanced = playhead + std::max(0, samples);
        if (! hasValidCycle(cycleActive, cycleStart, cycleEnd) || advanced < cycleEnd)
            return advanced;

        const auto cycleLength = cycleEnd - cycleStart;
        return cycleStart + std::fmod(advanced - cycleEnd, cycleLength);
    }
};
