#include "TimelineZoomPolicy.h"

#include <algorithm>

namespace TimelineZoomPolicy
{
TimelineViewportState zoomAroundSample(TimelineViewportState state, double pivotSample,
                                      float requestedZoom, double pixelsPerSecondAt1x,
                                      double sampleRate, float pivotX) noexcept
{
    state.horizontalZoom = std::clamp(requestedZoom, minimumHorizontalZoom, maximumHorizontalZoom);
    const auto pixelsPerSecond = pixelsPerSecondAt1x * state.horizontalZoom;
    state.viewStartSample = std::max(0.0, pivotSample - static_cast<double>(pivotX) / pixelsPerSecond * sampleRate);
    return state;
}

TimelineViewportState fitProject(TimelineViewportState state, double projectEndSample,
                                 int viewportWidth, int viewportHeight, int trackCount,
                                 double sampleRate) noexcept
{
    const auto end = std::max(sampleRate * 4.0, projectEndSample);
    state.viewStartSample = 0.0;
    state.horizontalZoom = std::clamp(static_cast<float>(viewportWidth * sampleRate / (80.0 * end * 1.03)),
                                      minimumHorizontalZoom, maximumHorizontalZoom);
    state.trackHeight = std::clamp(viewportHeight / std::max(1, trackCount), minimumTrackHeight, 96);
    return state;
}

TimelineViewportState fitSelection(TimelineViewportState state, double selectionStartSample,
                                   double selectionEndSample, int viewportWidth, int selectedTrackCount,
                                   int viewportHeight, double sampleRate) noexcept
{
    const auto start = std::max(0.0, std::min(selectionStartSample, selectionEndSample));
    const auto length = std::max(sampleRate / 64.0, std::abs(selectionEndSample - selectionStartSample));
    state.horizontalZoom = std::clamp(static_cast<float>(viewportWidth * sampleRate / (80.0 * length * 1.05)),
                                      minimumHorizontalZoom, maximumHorizontalZoom);
    state.viewStartSample = start;
    if (selectedTrackCount > 0)
        state.trackHeight = std::clamp(viewportHeight / selectedTrackCount, minimumTrackHeight, maximumTrackHeight);
    return state;
}
}
