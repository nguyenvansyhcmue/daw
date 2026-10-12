#pragma once

#include <algorithm>
#include <cmath>

// Pure logical-coordinate clip interaction policy. TimelineGrid converts its
// samples/scroll/zoom coordinates before calling this module, so the policy is
// reusable by other arrange-like views and straightforward to unit test.
enum class AudioRegionMouseZone { none, move, leftTrim, rightTrim, fadeIn, fadeOut, loop };
enum class AudioRegionInteractionPhase { idle, hover, pressed, dragging, committing, cancelled };

struct AudioRegionBounds
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

enum class AudioRegionCursorIntent { normal, move, trim, fade };

inline bool isAudioRegionEdgeZone(AudioRegionMouseZone zone) noexcept
{
    return zone == AudioRegionMouseZone::leftTrim || zone == AudioRegionMouseZone::rightTrim;
}

inline bool isAudioRegionFadeZone(AudioRegionMouseZone zone) noexcept
{
    return zone == AudioRegionMouseZone::fadeIn || zone == AudioRegionMouseZone::fadeOut;
}

inline AudioRegionMouseZone applyAudioRegionModifiers(AudioRegionMouseZone zone, bool fadeModifierDown) noexcept
{
    if (! fadeModifierDown)
        return zone;
    if (zone == AudioRegionMouseZone::leftTrim)
        return AudioRegionMouseZone::fadeIn;
    if (zone == AudioRegionMouseZone::rightTrim)
        return AudioRegionMouseZone::fadeOut;
    return zone;
}

inline AudioRegionCursorIntent audioRegionCursorIntent(AudioRegionMouseZone zone) noexcept
{
    if (zone == AudioRegionMouseZone::move)
        return AudioRegionCursorIntent::move;
    if (isAudioRegionFadeZone(zone))
        return AudioRegionCursorIntent::fade;
    if (zone == AudioRegionMouseZone::loop)
        return AudioRegionCursorIntent::fade;
    if (isAudioRegionEdgeZone(zone))
        return AudioRegionCursorIntent::trim;
    return AudioRegionCursorIntent::normal;
}

inline bool isAudioRegionLoopHandle(float mouseX, float mouseY, const AudioRegionBounds& bounds,
                                    float handleWidth = 14.0f, float handleHeight = 14.0f) noexcept
{
    return mouseX >= bounds.x + std::max(0.0f, bounds.width - handleWidth)
        && mouseX <= bounds.x + bounds.width
        && mouseY >= bounds.y && mouseY <= bounds.y + std::min(bounds.height, handleHeight);
}

inline AudioRegionMouseZone detectAudioRegionMouseZone(float mouseX, float mouseY,
                                                        const AudioRegionBounds& bounds,
                                                        float edgeThreshold = 8.0f) noexcept
{
    if (! std::isfinite(mouseX) || ! std::isfinite(mouseY) || bounds.width <= 0.0f || bounds.height <= 0.0f)
        return AudioRegionMouseZone::none;

    const auto right = bounds.x + bounds.width;
    const auto bottom = bounds.y + bounds.height;
    if (mouseX < bounds.x || mouseX > right || mouseY < bounds.y || mouseY > bottom)
        return AudioRegionMouseZone::none;

    // Edges always win over body movement. On short regions the nearest edge
    // wins, avoiding an ambiguous or accidentally moving target.
    const auto edge = std::max(1.0f, std::min(edgeThreshold, bounds.width * 0.5f));
    const auto left = std::abs(mouseX - bounds.x) <= edge;
    const auto rightEdge = std::abs(mouseX - right) <= edge;
    const auto useLeft = left && (! rightEdge || std::abs(mouseX - bounds.x) <= std::abs(mouseX - right));
    if (useLeft) return AudioRegionMouseZone::leftTrim;
    if (rightEdge) return AudioRegionMouseZone::rightTrim;
    return AudioRegionMouseZone::move;
}

inline bool audioRegionDragThresholdExceeded(float downX, float downY, float currentX, float currentY,
                                             float threshold = 4.0f) noexcept
{
    return std::hypot(currentX - downX, currentY - downY) >= threshold;
}
