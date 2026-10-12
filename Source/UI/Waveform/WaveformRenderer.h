#pragma once

#include "WaveformRenderRequest.h"
#include "WaveformRenderStyle.h"

class WaveformRenderer final
{
public:
    explicit WaveformRenderer(WaveformRenderStyle styleIn = {}) : style(std::move(styleIn)) {}
    void render(juce::Graphics&, const WaveformRenderRequest&) const;

private:
    float resolveDisplayReference(const WaveformRenderRequest&, int channel) const noexcept;
    void drawChannel(juce::Graphics&, const WaveformRenderRequest&, juce::Rectangle<float>,
                     int channel, float displayReference) const;
    void drawFades(juce::Graphics&, const WaveformRenderRequest&, juce::Rectangle<float>) const;

    WaveformRenderStyle style;
};
