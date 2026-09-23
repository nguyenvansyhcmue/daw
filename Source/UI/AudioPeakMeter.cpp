#include "AudioPeakMeter.h"

#include "Theme/StudioForgeLookAndFeel.h"

AudioPeakMeter::AudioPeakMeter()
{
    startTimerHz(60);
}

void AudioPeakMeter::updatePeak(float newLevel) noexcept
{
    updateStereoPeak(newLevel, newLevel);
}

void AudioPeakMeter::updateStereoPeak(float leftLevel, float rightLevel) noexcept
{
    const auto publishPeak = [] (std::atomic<float>& destination, float level)
    {
        level = juce::jlimit(0.0f, 1.0f, level);
        auto current = destination.load(std::memory_order_relaxed);
        while (level > current && ! destination.compare_exchange_weak(current, level,
                                                                        std::memory_order_relaxed,
                                                                        std::memory_order_relaxed)) {}
    };
    if (leftLevel >= 0.999f || rightLevel >= 0.999f)
        clipLatched.store(true, std::memory_order_relaxed);
    publishPeak(currentPeak, leftLevel);
    publishPeak(currentRightPeak, rightLevel);
}

void AudioPeakMeter::resetClipIndicator() noexcept
{
    clipLatched.store(false, std::memory_order_relaxed);
}

void AudioPeakMeter::mouseDown(const juce::MouseEvent&)
{
    resetClipIndicator();
    repaint();
}

void AudioPeakMeter::timerCallback()
{
    const auto incomingPeak = currentPeak.exchange(0.0f, std::memory_order_relaxed);
    const auto incomingRightPeak = currentRightPeak.exchange(0.0f, std::memory_order_relaxed);
    const auto release = incomingPeak > 0.0f || incomingRightPeak > 0.0f ? 0.88f : 0.72f;
    displayedPeak = juce::jmax(incomingPeak, displayedPeak * release);
    displayedRightPeak = juce::jmax(incomingRightPeak, displayedRightPeak * release);
    repaint();
}

void AudioPeakMeter::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour(0xff101010));
    g.fillRoundedRectangle(bounds, 3.0f);
    const auto horizontal = bounds.getWidth() > bounds.getHeight() * 2.5f;
    const auto meterColour = [] (float fraction)
    {
        if (fraction > 0.96f) return StudioForgeTheme::clipRed;
        if (fraction > 0.88f) return StudioForgeTheme::warning;
        if (fraction > 0.70f) return StudioForgeTheme::meterGreen;
        if (fraction > 0.52f) return juce::Colour(0xff25cda5);
        if (fraction > 0.32f) return StudioForgeTheme::meterCyan;
        if (fraction > 0.17f) return juce::Colour(0xff20afcf);
        if (fraction > 0.07f) return juce::Colour(0xff297ec3);
        return juce::Colour(0xff3156a4);
    };
    const auto drawSegments = [&g, &meterColour] (juce::Rectangle<float> lane, float peak, bool isHorizontal)
    {
        const auto axisLength = isHorizontal ? lane.getWidth() : lane.getHeight();
        const auto segmentCount = juce::jlimit(36, 72, juce::roundToInt(axisLength / 3.0f));
        const auto segmentExtent = (isHorizontal ? lane.getWidth() : lane.getHeight()) / segmentCount;
        for (int index = 0; index < segmentCount; ++index)
        {
            const auto fraction = static_cast<float>(index + 1) / segmentCount;
            const auto isActive = fraction <= peak;
            auto segment = isHorizontal
                ? juce::Rectangle<float>(lane.getX() + index * segmentExtent, lane.getY(), segmentExtent - 0.55f, lane.getHeight())
                : juce::Rectangle<float>(lane.getX(), lane.getBottom() - (index + 1) * segmentExtent, lane.getWidth(), segmentExtent - 0.55f);
            g.setColour(isActive ? meterColour(fraction) : juce::Colour(0xff172126));
            g.fillRect(segment);
        }
    };
    auto lanes = bounds.reduced(horizontal ? 16.0f : 2.0f, 4.0f);
    const auto leftBounds = horizontal ? lanes.removeFromTop((lanes.getHeight() - 2.0f) * 0.5f) : lanes;
    if (horizontal) lanes.removeFromTop(2.0f);
    const auto rightBounds = horizontal ? lanes : juce::Rectangle<float>();
    drawSegments(leftBounds, displayedPeak, horizontal);
    if (horizontal) drawSegments(rightBounds, displayedRightPeak, true);

    if (horizontal)
    {
        g.setColour(StudioForgeTheme::secondaryText.withAlpha(0.78f));
        g.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
        g.drawText("L", bounds.getX() + 2.0f, leftBounds.getY() - 1.0f, 10.0f, leftBounds.getHeight() + 2.0f, juce::Justification::centred, false);
        g.drawText("R", bounds.getX() + 2.0f, rightBounds.getY() - 1.0f, 10.0f, rightBounds.getHeight() + 2.0f, juce::Justification::centred, false);
    }

    if (clipLatched.load(std::memory_order_relaxed))
    {
        g.setColour(juce::Colours::red);
        g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 3.0f);
    }
}
