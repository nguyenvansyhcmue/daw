#pragma once

#include <algorithm>
#include <cmath>

// A small, allocation-free gate between F0 analysis and the live renderer.
// It deliberately does not decide pitch targets or change renderer timing.
// The lower hold threshold and short hangover stop vibrato/consonants from
// repeatedly opening and closing the wet path during a sustained phrase.
class LiveTuneConfidenceGate
{
public:
    static constexpr float enterConfidence = 0.52f;
    static constexpr float holdConfidence = 0.42f;
    static constexpr double dropoutHoldSeconds = 0.035;

    void reset() noexcept
    {
        open = false;
        remainingHoldSamples = 0;
    }

    bool advance(bool trackerVoiced, float confidence, int callbackSamples, double sampleRate) noexcept
    {
        const auto finiteConfidence = std::isfinite(confidence);
        const auto threshold = open ? holdConfidence : enterConfidence;
        const auto confident = trackerVoiced && finiteConfidence && confidence >= threshold;

        if (confident)
        {
            open = true;
            remainingHoldSamples = std::max(1, static_cast<int>(std::ceil(
                std::max(1.0, sampleRate) * dropoutHoldSeconds)));
        }
        else if (open && remainingHoldSamples > 0)
        {
            remainingHoldSamples = std::max(0, remainingHoldSamples - std::max(1, callbackSamples));
            if (remainingHoldSamples == 0)
                open = false;
        }
        else
        {
            open = false;
        }

        return open;
    }

    bool isOpen() const noexcept { return open; }

private:
    bool open = false;
    int remainingHoldSamples = 0;
};
