#include "AudioPeakMeter.h"

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
    displayedPeak = juce::jmax(incomingPeak, displayedPeak * 0.92f);
    displayedRightPeak = juce::jmax(incomingRightPeak, displayedRightPeak * 0.92f);
    repaint();
}

void AudioPeakMeter::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour(0xff101010));
    g.fillRoundedRectangle(bounds, 3.0f);

    g.setColour(juce::Colours::black.withAlpha(0.62f));
    for (const auto fraction : { 0.25f, 0.5f, 0.75f })
    {
        const auto y = bounds.getBottom() - bounds.getHeight() * fraction;
        g.drawHorizontalLine(juce::roundToInt(y), bounds.getX(), bounds.getRight());
    }

    const auto leftBounds = bounds.withWidth((bounds.getWidth() - 2.0f) * 0.5f);
    const auto rightBounds = leftBounds.withX(leftBounds.getRight() + 2.0f);
    juce::ColourGradient gradient(juce::Colour(0xff23d18b), 0.0f, bounds.getBottom(),
                                  juce::Colour(0xffffd166), 0.0f, bounds.getY(), false);
    gradient.addColour(0.65, juce::Colour(0xffffd166));
    gradient.addColour(0.88, juce::Colour(0xffef5350));
    g.setGradientFill(gradient);
    const auto drawLevel = [&g, &gradient, bounds] (juce::Rectangle<float> channel, float peak)
    {
        g.setGradientFill(gradient);
        g.fillRoundedRectangle(channel.withTop(bounds.getBottom() - channel.getHeight() * peak), 2.0f);
    };
    drawLevel(leftBounds, displayedPeak);
    drawLevel(rightBounds, displayedRightPeak);

    if (clipLatched.load(std::memory_order_relaxed))
    {
        g.setColour(juce::Colours::red);
        g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 3.0f);
    }
}
