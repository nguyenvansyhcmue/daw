#pragma once

#include "TimelineViewportState.h"

namespace TimelineZoomPolicy
{
inline constexpr float minimumHorizontalZoom = 0.5f;
inline constexpr float maximumHorizontalZoom = 4096.0f;
inline constexpr int minimumTrackHeight = 28;
inline constexpr int maximumTrackHeight = 160;
inline constexpr float minimumWaveformZoom = 0.25f;
inline constexpr float maximumWaveformZoom = 8.0f;

TimelineViewportState zoomAroundSample(TimelineViewportState state, double pivotSample,
                                      float requestedZoom, double pixelsPerSecondAt1x,
                                      double sampleRate, float pivotX) noexcept;
TimelineViewportState fitProject(TimelineViewportState state, double projectEndSample,
                                 int viewportWidth, int viewportHeight, int trackCount,
                                 double sampleRate) noexcept;
TimelineViewportState fitSelection(TimelineViewportState state, double selectionStartSample,
                                   double selectionEndSample, int viewportWidth, int selectedTrackCount,
                                   int viewportHeight, double sampleRate) noexcept;
}
