#pragma once

#include <juce_graphics/juce_graphics.h>

#include "../../Media/WaveformThumbnailCache.h"
#include "../../Models/AudioClipState.h"

enum class WaveformNormalization : uint8_t
{
    sourceAccurate,
    clipAdaptive,
    detailAssist
};

struct WaveformRenderState
{
    bool selected = false;
    bool muted = false;
    bool hover = false;
};

struct WaveformRenderRequest
{
    const AudioClipState& clip;
    const WaveformThumbnail* thumbnail = nullptr;
    juce::Rectangle<float> bounds;
    WaveformRenderState state;
    WaveformNormalization normalization = WaveformNormalization::clipAdaptive;
    float amplitudeZoom = 1.0f;
};
