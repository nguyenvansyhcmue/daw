#include "Plugins/LiveTuneConfidenceGate.h"

#include <iostream>
#include <limits>

namespace
{
bool survivesDropout(int callbackSamples)
{
    LiveTuneConfidenceGate gate;
    if (gate.advance(true, 0.51f, callbackSamples, 48000.0)) return false;
    if (! gate.advance(true, 0.52f, callbackSamples, 48000.0)) return false;
    if (! gate.advance(true, 0.43f, callbackSamples, 48000.0)) return false;

    int elapsedSamples = 0;
    while (elapsedSamples + callbackSamples < 1680)
    {
        if (! gate.advance(false, 0.0f, callbackSamples, 48000.0)) return false;
        elapsedSamples += callbackSamples;
    }
    return ! gate.advance(false, 0.0f, callbackSamples, 48000.0);
}
}

int main()
{
    for (const auto callbackSamples : { 32, 64, 128 })
        if (! survivesDropout(callbackSamples))
        {
            std::cerr << "Confidence gate hysteresis failed at " << callbackSamples << " samples\n";
            return 1;
        }

    LiveTuneConfidenceGate gate;
    if (! gate.advance(true, 0.80f, 64, 48000.0))
    {
        std::cerr << "Confidence gate did not open for stable voice\n";
        return 1;
    }
    if (! gate.advance(true, std::numeric_limits<float>::quiet_NaN(), 64, 48000.0))
    {
        std::cerr << "Confidence gate dropped too early for one invalid tracker frame\n";
        return 1;
    }
    gate.reset();
    if (gate.isOpen())
    {
        std::cerr << "Confidence gate reset failed\n";
        return 1;
    }

    std::cout << "live_confidence_gate | 32=pass 64=pass 128=pass hysteresis=pass reset=pass\n";
    return 0;
}
